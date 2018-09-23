// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sapiserver.h"

#include "compat.h"
#include "util.h"
#include "netbase.h"
#include "rpc/protocol.h" // For HTTP status codes
#include "sync.h"
#include "ui_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/thread.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>

#ifdef EVENT__HAVE_NETINET_IN_H
#include <netinet/in.h>
#ifdef _XOPEN_SOURCE_EXTENDED
#include <arpa/inet.h>
#endif
#endif

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/foreach.hpp>

/** Maximum size of http request (request line + headers) */
static const size_t MAX_HEADERS_SIZE = 8192;

//! libevent event loop
static struct event_base* eventBaseSAPI = 0;
//! SAPI server
struct evhttp* eventSAPI = 0;
//! Work queue for handling longer requests off the event loop thread
static WorkQueue<HTTPClosure>* workQueue = 0;
//! Handlers for (sub)paths
static std::vector<HTTPPathHandler> pathHandlersSAPI;
//! Bound listening sockets
static std::vector<evhttp_bound_socket *> boundSocketsSAPI;

/** Check if a network address is allowed to access the HTTP server */
static bool ClientAllowed(const CNetAddr& netaddr)
{
    if (!netaddr.IsValid())
        return false;

    return true;
}

/** SAPI request callback */
static void sapi_request_cb(struct evhttp_request* req, void* arg)
{
    std::unique_ptr<HTTPRequest> hreq(new HTTPRequest(req));

    LogPrint("sapi", "Received a %s request for %s from %s\n",
             RequestMethodString(hreq->GetRequestMethod()), hreq->GetURI(), hreq->GetPeer().ToString());

    // Early address-based allow check
    if (!ClientAllowed(hreq->GetPeer())) {
        hreq->WriteReply(HTTP_FORBIDDEN, "Access forbidden");
        return;
    }

    // Early reject unknown HTTP methods
    if (hreq->GetRequestMethod() == HTTPRequest::UNKNOWN) {
        hreq->WriteReply(HTTP_BADMETHOD, "Invalid method");
        return;
    }

    // Find registered handler for prefix
    std::string strURI = hreq->GetURI();
    std::string path;
    std::vector<HTTPPathHandler>::const_iterator i = pathHandlersSAPI.begin();
    std::vector<HTTPPathHandler>::const_iterator iend = pathHandlersSAPI.end();
    for (; i != iend; ++i) {
        bool match = false;
        if (i->exactMatch)
            match = (strURI == i->prefix);
        else
            match = (strURI.substr(0, i->prefix.size()) == i->prefix);
        if (match) {
            path = strURI.substr(i->prefix.size());
            break;
        }
    }

    // Dispatch to worker thread
    if (i != iend) {
        std::unique_ptr<HTTPWorkItem> item(new HTTPWorkItem(std::move(hreq), path, i->handler));
        assert(workQueue);
        if (workQueue->Enqueue(item.get()))
            item.release(); /* if true, queue took ownership */
        else {
            LogPrintf("WARNING: request rejected because sapi work queue depth exceeded, it can be increased with the -sapiworkqueue= setting\n");
            item->req->WriteReply(HTTP_INTERNAL, "Work queue depth exceeded");
        }
    } else {
        hreq->WriteReply(HTTP_NOTFOUND, "Invalid endpoint");
    }
}

/** Callback to reject SAPI requests after shutdown. */
static void sapi_reject_request_cb(struct evhttp_request* req, void*)
{
    LogPrint("sapi", "Rejecting request while shutting down\n");
    evhttp_send_error(req, HTTP_SERVUNAVAIL, NULL);
}

/** Event dispatcher thread */
static void ThreadSAPI(struct event_base* base, struct evhttp* http)
{
    RenameThread("smartcash-sapi");
    LogPrint("sapi", "Entering sapi event loop\n");
    event_base_dispatch(base);
    // Event loop will be interrupted by InterruptSAPIServer()
    LogPrint("sapi", "Exited sapi event loop\n");
}

/** Bind SAPI server to specified addresses */
static bool SAPIBindAddresses(struct evhttp* http)
{
    int defaultPort = DEFAULT_SAPI_SERVER_PORT;
    std::vector<std::pair<std::string, uint16_t> > endpoints;

    endpoints.push_back(std::make_pair("::", defaultPort));
    endpoints.push_back(std::make_pair("0.0.0.0", defaultPort));

    // Bind addresses
    for (std::vector<std::pair<std::string, uint16_t> >::iterator i = endpoints.begin(); i != endpoints.end(); ++i) {
        LogPrint("sapi", "Binding SAPI on address %s port %i\n", i->first, i->second);
        evhttp_bound_socket *bind_handle = evhttp_bind_socket_with_handle(http, i->first.empty() ? NULL : i->first.c_str(), i->second);
        if (bind_handle) {
            boundSocketsSAPI.push_back(bind_handle);
        } else {
            LogPrintf("Binding SAPI on address %s port %i failed.\n", i->first, i->second);
        }
    }
    return !boundSocketsSAPI.empty();
}

/** Simple wrapper to set thread name and run work queue */
static void SAPIWorkQueueRun(WorkQueue<HTTPClosure>* queue)
{
    RenameThread("smartcash-sapiworker");
    queue->Run();
}

/** libevent event log callback */
static void libevent_log_cb(int severity, const char *msg)
{
#ifndef EVENT_LOG_WARN
// EVENT_LOG_WARN was added in 2.0.19; but before then _EVENT_LOG_WARN existed.
# define EVENT_LOG_WARN _EVENT_LOG_WARN
#endif
    if (severity >= EVENT_LOG_WARN) // Log warn messages and higher without debug category
        LogPrintf("libevent: %s\n", msg);
    else
        LogPrint("libevent", "libevent: %s\n", msg);
}

bool InitSAPIServer()
{
    struct evhttp* sapi = 0;
    struct event_base* base = 0;

    // Redirect libevent's logging to our own log
    event_set_log_callback(&libevent_log_cb);
#if LIBEVENT_VERSION_NUMBER >= 0x02010100
    // If -debug=libevent, set full libevent debugging.
    // Otherwise, disable all libevent debugging.
    if (LogAcceptCategory("libevent"))
        event_enable_debug_logging(EVENT_DBG_ALL);
    else
        event_enable_debug_logging(EVENT_DBG_NONE);
#endif
#ifdef WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif

    base = event_base_new(); // XXX RAII
    if (!base) {
        LogPrintf("Couldn't create an event_base: exiting\n");
        return false;
    }

    /* Create a new evhttp object to handle requests. */
    sapi = evhttp_new(base); // XXX RAII
    if (!sapi) {
        LogPrintf("couldn't create evhttp for SAPI. Exiting.\n");
        event_base_free(base);
        return false;
    }

    evhttp_set_timeout(sapi, GetArg("-sapiservertimeout", DEFAULT_SAPI_SERVER_TIMEOUT));
    evhttp_set_max_headers_size(sapi, MAX_HEADERS_SIZE);
    evhttp_set_max_body_size(sapi, MAX_SIZE);
    evhttp_set_gencb(sapi, sapi_request_cb, NULL);
    evhttp_set_allowed_methods(sapi, EVHTTP_REQ_GET |
                                       EVHTTP_REQ_POST |
                                       EVHTTP_REQ_OPTIONS);

    if (!SAPIBindAddresses(sapi)) {
        LogPrintf("Unable to bind any endpoint for SAPI server\n");
        evhttp_free(sapi);
        event_base_free(base);
        return false;
    }

    LogPrint("sapi", "Initialized SAPI server\n");
    int workQueueDepth = std::max((long)GetArg("-sapiworkqueue", DEFAULT_SAPI_WORKQUEUE), 1L);
    LogPrintf("SAPI: creating work queue of depth %d\n", workQueueDepth);

    workQueue = new WorkQueue<HTTPClosure>(workQueueDepth);
    eventBaseSAPI = base;
    eventSAPI = sapi;
    return true;
}

boost::thread threadSAPI;

bool StartSAPIServer()
{
    LogPrint("sapi", "Starting SAPI server\n");
    int rpcThreads = std::max((long)GetArg("-sapithreads", DEFAULT_SAPI_THREADS), 1L);
    LogPrintf("SAPI: starting %d worker threads\n", rpcThreads);
    threadSAPI = boost::thread(boost::bind(&ThreadSAPI, eventBaseSAPI, eventSAPI));

    for (int i = 0; i < rpcThreads; i++)
        boost::thread(boost::bind(&SAPIWorkQueueRun, workQueue));
    return true;
}

void InterruptSAPIServer()
{
    LogPrint("sapi", "Interrupting SAPI server\n");
    if (eventSAPI) {
        // Unlisten sockets
        BOOST_FOREACH (evhttp_bound_socket *socket, boundSocketsSAPI) {
            evhttp_del_accept_socket(eventSAPI, socket);
        }
        // Reject requests on current connections
        evhttp_set_gencb(eventSAPI, sapi_reject_request_cb, NULL);
    }
    if (workQueue)
        workQueue->Interrupt();
}

void StopSAPIServer()
{
    LogPrint("sapi", "Stopping HTTP server\n");
    if (workQueue) {
        LogPrint("sapi", "Waiting for SAPI worker threads to exit\n");
        workQueue->WaitExit();
        delete workQueue;
    }
    if (eventBaseSAPI) {
        LogPrint("sapi", "Waiting for SAPI event thread to exit\n");
        // Give event loop a few seconds to exit (to send back last SAPI responses), then break it
        // Before this was solved with event_base_loopexit, but that didn't work as expected in
        // at least libevent 2.0.21 and always introduced a delay. In libevent
        // master that appears to be solved, so in the future that solution
        // could be used again (if desirable).
        // (see discussion in https://github.com/bitcoin/bitcoin/pull/6990)
#if BOOST_VERSION >= 105000
        if (!threadSAPI.try_join_for(boost::chrono::milliseconds(2000))) {
#else
        if (!threadSAPI.timed_join(boost::posix_time::milliseconds(2000))) {
#endif
            LogPrintf("SAPI event loop did not exit within allotted time, sending loopbreak\n");
            event_base_loopbreak(eventBaseSAPI);
            threadSAPI.join();
        }
    }
    if (eventSAPI) {
        evhttp_free(eventSAPI);
        eventSAPI = 0;
    }
    if (eventBaseSAPI) {
        event_base_free(eventBaseSAPI);
        eventBaseSAPI = 0;
    }
    LogPrint("sapi", "Stopped SAPI server\n");
}

void RegisterSAPIHandler(const std::string &prefix, bool exactMatch, const HTTPRequestHandler &handler)
{
    LogPrint("sapi", "Registering SAPI handler for %s (exactmatch %d)\n", prefix, exactMatch);
    pathHandlersSAPI.push_back(HTTPPathHandler(prefix, exactMatch, handler));
}

void UnregisterSAPIHandler(const std::string &prefix, bool exactMatch)
{
    std::vector<HTTPPathHandler>::iterator i = pathHandlersSAPI.begin();
    std::vector<HTTPPathHandler>::iterator iend = pathHandlersSAPI.end();
    for (; i != iend; ++i)
        if (i->prefix == prefix && i->exactMatch == exactMatch)
            break;
    if (i != iend)
    {
        LogPrint("sapi", "Unregistering HTTP handler for %s (exactmatch %d)\n", prefix, exactMatch);
        pathHandlersSAPI.erase(i);
    }
}

