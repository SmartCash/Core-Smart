// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chain.h"
#include "clientversion.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "validation.h"
#include "smartnode/smartnodesync.h"
#include "sapi/sapi.h"
#include "sapi/sapi_validation.h"
#include "streams.h"
#include "sync.h"
#include "rpc/client.h"
#include "txmempool.h"
#include "utilstrencodings.h"
#include "version.h"

#include <boost/algorithm/string.hpp>
#include <boost/dynamic_bitset.hpp>

#include <univalue.h>

#include "sapi/sapi_address.h"
#include "sapi/sapi_blockchain.h"
#include "sapi/sapi_common.h"
#include "sapi/sapi_transaction.h"
#include "sapi/sapi_smartnodes.h"
#include "sapi/sapi_smartrewards.h"


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

static const int DEFAULT_SAPI_THREADS=4;
static const int DEFAULT_SAPI_WORKQUEUE=16;
static const int DEFAULT_SAPI_SERVER_TIMEOUT=30;
static const int DEFAULT_SAPI_SERVER_PORT=8080;

static const int DEFAULT_SAPI_JSON_INDENT=2;

// SAPI Version
static const int  SAPI_VERSION_MAJOR = 1;
static const int  SAPI_VERSION_MINOR = 0;

extern std::string strClientVersion;

std::string SAPI::versionSubPath;
std::string SAPI::versionString;

static int64_t nStartTime;

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

// Endpoint groups available for the SAPI
static std::vector<SAPI::EndpointGroup*> endpointGroups;

std::vector<CSubNet> vecWhitelistedRange;

CSAPIStatistics sapiStatistics;

using namespace std;

static bool SAPIExecuteEndpoint(HTTPRequest *req, const std::map<std::string, std::string> &mapPathParams, const SAPI::Endpoint *endpoint);

static void SplitPath(const std::string& str, std::vector<std::string>& parts,
              const std::string& delim = "/")
{
    boost::split(parts, str, boost::is_any_of(delim));
}

/** Check if a network address is allowed to access the HTTP server */
static bool ClientAllowed(const CNetAddr& netaddr)
{
    if (!netaddr.IsValid())
        return false;

    return true;
}

UniValue UniValueFromAmount(int64_t nAmount)
{
    bool sign = nAmount < 0;
    int64_t n_abs = (sign ? -nAmount : nAmount);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
    return UniValue(UniValue::VNUM,
            strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder));
}

/** SAPI request callback */
static void sapi_request_cb(struct evhttp_request* req, void* arg)
{
    std::unique_ptr<HTTPRequest> hreq(new HTTPRequest(req));
    HTTPRequest::RequestMethod method = hreq->GetRequestMethod();
    LogPrint("sapi", "Received a %s request for %s from %s\n",
             RequestMethodString(hreq->GetRequestMethod()), hreq->GetURI(), hreq->GetPeer().ToString());

    if (!SAPI::CheckWarmup(hreq.get()))
        return;

    CService peer = hreq->GetPeer();

    // Early address-based allow check
    if (!ClientAllowed(peer)) {
        sapiStatistics.request(peer, CSAPIStatistics::Blocked);
        SAPI::Error(hreq.get(), HTTPStatus::FORBIDDEN, "Access forbidden");
        return;
    }

    bool fWhitelisted = SAPI::IsWhitelistedRange(peer);

    if( !fWhitelisted ){

        SAPI::Limits::Client * client = SAPI::Limits::GetClient(peer);

        client->Request();

        // Check the rate limiting for this peer
        if( client->IsRequestLimited() ){
            sapiStatistics.request(peer, CSAPIStatistics::Blocked);
            SAPI::Result error(SAPI::RequestRateLimitExceeded,
                               strprintf("Cool down! Requests locked for %d seconds", client->GetRequestLockSeconds()));
            SAPI::Error(hreq.get(), HTTPStatus::FORBIDDEN, error);
            return;
        }

        if( client->IsRessourceLimited() ){
            sapiStatistics.request(peer, CSAPIStatistics::Blocked);
            SAPI::Result error(SAPI::RessourceRateLimitExceeded,
                               strprintf("Cool down! Ressources locked for %d seconds", client->GetRessourceLockSeconds()));
            SAPI::Error(hreq.get(), HTTPStatus::FORBIDDEN, error);
            return;
        }

    }

    // Early reject unknown HTTP methods
    if (method == HTTPRequest::UNKNOWN) {
        sapiStatistics.request(peer, CSAPIStatistics::Invalid);
        SAPI::Error(hreq.get(), HTTPStatus::BAD_METHOD, "Invalid method");
        return;
    }

    // Get the requested path
    std::string strURI = hreq->GetURI();

    // For now we only have v1, so just check if its provided..
    if( strURI.substr(0,SAPI::versionSubPath.size()) != SAPI::versionSubPath ){
        sapiStatistics.request(peer, CSAPIStatistics::Invalid);
        SAPI::Error(hreq.get(), HTTPStatus::NOT_FOUND, "Invalid api version. Use: <host>/v1/<endpoint>");
        return;
    }

    strURI = strURI.substr(SAPI::versionSubPath.size());

    // Check if there is anything else provided after the version
    if( !strURI.size() || strURI.front() != '/' ){
        sapiStatistics.request(peer, CSAPIStatistics::Invalid);
        SAPI::Error(hreq.get(), HTTPStatus::NOT_FOUND, "Endpoint missing. Use: <host>/v1/<endpoint>");
        return;
    }

    std::vector<std::string> partsURI;
    std::map<SAPI::Endpoint*, std::map<std::string, std::string>> mapPathMatch;

    SplitPath(strURI.substr(1), partsURI);

    std::string pathGroup = partsURI.front();

    // Get a subvector without the path group
    partsURI = std::vector<std::string>( partsURI.begin() + 1, partsURI.end() );

    for( auto group : endpointGroups ){

        if( group->prefix != pathGroup )
            continue;

        for( SAPI::Endpoint &endpoint : group->endpoints ){

            std::vector<std::string> partsEndpoint;
            SplitPath(endpoint.path, partsEndpoint);

            if( partsEndpoint.size() == 1 && partsEndpoint.back() == "" ){
                // If the endpoint is the root one for the group /v1/<group>

                // Match /v1/<group>/<endpoint> and /v1/<group>/<endpoint>/
                if( !partsURI.size() || ( partsURI.size() == 1 && partsURI.back() == "" ) )
                    mapPathMatch.insert(std::make_pair(&endpoint, std::map<std::string, std::string>()));

            // Match /v1/<group>/<endpoint> and /v1/<group>/<endpoint>/
            // For any other possible matching endpoints /v1/<group>/../..
            }else if( ( partsURI.size() == partsEndpoint.size() + 1 && partsURI.back() == "" ) ||
                        partsURI.size() == partsEndpoint.size() ){

                bool fMatch = true;
                std::map<std::string, std::string> mapPathParams;

                for( size_t i = 0; i < partsURI.size(); i++ ){

                    bool fParam = false;
                    std::string partStr = i < partsEndpoint.size() ? partsEndpoint.at(i) : "";
                    std::string &uriPartStr = partsURI.at(i);

                    // Check if a parameter is expected for this path component
                    if( partStr.front() == '{' && partStr.back() == '}' )
                        fParam = true;

                    if( uriPartStr != partStr && !fParam ){
                        fMatch = false;
                        break;
                    }else if( fParam ){

                        // Filter the param key of the path part
                        partStr = std::string(partStr.begin() + 1, partStr.end() - 1 );

                        // Add the potential path parameter
                        mapPathParams.insert(std::make_pair(partStr, uriPartStr));
                    }
                }

                if( fMatch )
                    mapPathMatch.insert(std::make_pair(&endpoint, mapPathParams));

            }
        }
    }

    if( mapPathMatch.size() && hreq->GetRequestMethod() == HTTPRequest::OPTIONS){

        sapiStatistics.request(peer, CSAPIStatistics::Valid);

        std::string strMethods = RequestMethodString(HTTPRequest::OPTIONS);

        for( auto match : mapPathMatch ){
            strMethods += ", " + RequestMethodString(match.first->method);
        }

        // For options requests just answer with the allowed methods for this endpoint.
        SAPI::AddDefaultHeaders(hreq.get());
        hreq->WriteHeader("Access-Control-Allow-Methods", strMethods);
        hreq->WriteHeader("Access-Control-Allow-Headers", "Content-Type");
        hreq->WriteReply(HTTPStatus::OK, std::string());
        return;
    }

    auto fullMatch = std::find_if(mapPathMatch.begin(), mapPathMatch.end(),
                                  [method](const std::pair<const SAPI::Endpoint *,
                                  std::map<std::string, std::string>> &entry) -> bool{
        return method == entry.first->method;
    });

    // Dispatch to worker thread
    if (fullMatch != mapPathMatch.end()) {

        sapiStatistics.request(peer, CSAPIStatistics::Valid);

        std::unique_ptr<SAPIWorkItem> item(new SAPIWorkItem(std::move(hreq), fullMatch->second, fullMatch->first, SAPIExecuteEndpoint));
        assert(workQueue);
        if (workQueue->Enqueue(item.get()))
            item.release(); /* if true, queue took ownership */
        else {
            LogPrintf("WARNING: request rejected because sapi work queue depth exceeded, it can be increased with the -sapiworkqueue= setting\n");
            item->req->WriteReply(HTTPStatus::INTERNAL_SERVER_ERROR, "Work queue depth exceeded");
        }
    } else {
        sapiStatistics.request(peer, CSAPIStatistics::Invalid);
        SAPI::Error(hreq.get(), HTTPStatus::NOT_FOUND, "Invalid endpoint: " + strURI + " with method: " + RequestMethodString(hreq->GetRequestMethod()));
    }

    // Clean up rate limits
    SAPI::Limits::CheckAndRemove();
}

/** Callback to reject SAPI requests after shutdown. */
static void sapi_reject_request_cb(struct evhttp_request* req, void*)
{
    LogPrint("sapi", "Rejecting request while shutting down\n");
    evhttp_send_error(req, HTTPStatus::SERVICE_UNAVAILABLE, NULL);
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
    uint16_t defaultPort = GetArg("-sapiport", DEFAULT_SAPI_SERVER_PORT);
    std::vector<std::pair<std::string, uint16_t> > endpoints;

    endpoints.push_back(std::make_pair("0.0.0.0", defaultPort));
    endpoints.push_back(std::make_pair("::", defaultPort));

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


void SAPI::AddWhitelistedRange(const CSubNet &subnet) {
    vecWhitelistedRange.push_back(subnet);
}

bool SAPI::IsWhitelistedRange(const CNetAddr &addr) {
    BOOST_FOREACH(const CSubNet& subnet, vecWhitelistedRange) {
        if (subnet.Match(addr))
            return true;
    }
    return false;
}

bool InitSAPIServer()
{
    nStartTime = GetTime();

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


static SAPI::Result ParameterBaseCheck(HTTPRequest* req, const UniValue &obj, const SAPI::BodyParameter &param)
{
    std::string key = param.key;
    SAPI::Codes code = SAPI::Valid;
    std::string err = std::string();

    if( !obj.exists(param.key) && !param.optional ){
        err = "Parameter missing: " + key;
        code = SAPI::ParameterMissing;
    }else if( obj.exists(param.key) ){

        if( obj[key].type() != param.validator->GetType() ){

            code = SAPI::InvalidType;
            err = "Invalid type for key: " + key;

            switch( param.validator->GetType() ){
                case UniValue::VARR:
                     err += " -- expected JSON-Array";
                    break;
                case UniValue::VBOOL:
                    err += " -- expected Bool";
                    break;
                case UniValue::VNULL:
                     err += " -- expected Null";
                    break;
                case UniValue::VNUM:
                     err += " -- expected Number";
                    break;
                case UniValue::VOBJ:
                    err += " -- expected Object";
                    break;
                case UniValue::VSTR:
                    err += " -- expected String";
                    break;
                default:
                    err = "ParameterBaseCheck: invalid type value.";
                    break;
                }
        }

    }

    if( code != SAPI::Valid ){
        return SAPI::Result(code, err);
    }

    return SAPI::Result();
}

bool ParseHashStr(const string& strHash, uint256& v)
{
    if (!IsHex(strHash) || (strHash.size() != 64))
        return false;

    v.SetHex(strHash);
    return true;
}

bool SAPI::CheckWarmup(HTTPRequest* req)
{
    std::string statusmessage;
    if (RPCIsInWarmup(&statusmessage))
        return SAPI::Error(req, HTTPStatus::SERVICE_UNAVAILABLE, "Service temporarily unavailable: " + statusmessage);
    if(!smartnodeSync.IsBlockchainSynced())
        return SAPI::Error(req, HTTPStatus::SERVICE_UNAVAILABLE, "Service temporarily unavailable: Syncing with the SmartCash network.");
    return true;
}

bool StartSAPI()
{
    SAPI::versionSubPath = strprintf("/v%d", SAPI_VERSION_MAJOR);
    SAPI::versionString = strprintf("%d.%d", SAPI_VERSION_MAJOR, SAPI_VERSION_MINOR);

    endpointGroups = {
        &clientEndpoints,
        &statisticEndpoints,
        &blockchainEndpoints,
        &addressEndpoints,
        &transactionEndpoints,
        &smartnodesEndpoints,
        &smartrewardsEndpoints,
    };

    return true;
}

void InterruptSAPI()
{
    // Nothing to do here yet.
}

void StopSAPI()
{
    // Nothing to do here yet.
}

static bool SAPIValidateBody(HTTPRequest *req, const SAPI::Endpoint *endpoint, UniValue &bodyParameter)
{

    if( endpoint->bodyRoot != UniValue::VARR && endpoint->bodyRoot != UniValue::VOBJ )
        return true;

    std::string bodyStr = req->ReadBody();

    if ( bodyStr.empty() )
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "No body parameter object defined in the body: {...TBD...}");

    try{
        // Try to parse body string to json
        UniValue jVal;
        if (!jVal.read(std::string("[")+bodyStr+std::string("]")) ||
            !jVal.isArray() || jVal.size()!=1)
            throw runtime_error(string("Error parsing JSON:")+bodyStr);
        bodyParameter = jVal[0];
    }
    catch (UniValue& objError)
    {
            try // Nice formatting for standard-format error
            {
                int code = find_value(objError, "code").get_int();
                std::string message = find_value(objError, "message").get_str();
                return SAPI::Error(req, HTTPStatus::BAD_REQUEST, message + " (code " + std::to_string(code) + ")");
            }
            catch (const std::runtime_error&) // raised when converting to invalid type, i.e. missing code or message
            {   // Show raw JSON object
                return SAPI::Error(req, HTTPStatus::BAD_REQUEST, objError.write());
            }
    }
    catch (const std::exception& e)
    {
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "Error: " + std::string(e.what()));
    }

    if( endpoint->bodyRoot == UniValue::VOBJ && !bodyParameter.isObject() )
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "Parameter json is expedted to be a JSON object: {...TBD... }");
    else if( endpoint->bodyRoot == UniValue::VARR && !bodyParameter.isArray() )
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "Parameter json is expedted to be a JSON array: {...TBD... }");

    std::vector<SAPI::Result> results;

    for( auto param : endpoint->vecBodyParameter ){

        SAPI::Result &&result = ParameterBaseCheck(req, bodyParameter, param);

        if( result != SAPI::Codes::Valid ){

            results.push_back(result);

        }else if( bodyParameter.exists(param.key) ){

            SAPI::Result result = param.validator->Validate(param.key, bodyParameter[param.key]);

            if( result != SAPI::Valid ){
                results.push_back(result);
            }
        }
    }

    if( results.size() )
        return SAPI::Error(req,HTTPStatus::BAD_REQUEST,results);

    return true;
}

static bool SAPIExecuteEndpoint(HTTPRequest *req, const std::map<std::string, std::string> &mapPathParams, const SAPI::Endpoint *endpoint)
{
    UniValue bodyParameter;

    if(!SAPIValidateBody(req, endpoint, bodyParameter) )
        return false;

    return endpoint->handler(req, mapPathParams, bodyParameter );
}

std::string JsonString(const UniValue &obj)
{
    return obj.write(DEFAULT_SAPI_JSON_INDENT) + "\n";
}

void SAPI::AddDefaultHeaders(HTTPRequest* req)
{
    req->WriteHeader("User-Agent", CLIENT_NAME);
    req->WriteHeader("Client-Version", strClientVersion);
    req->WriteHeader("SAPI-Version", SAPI::versionString);
    req->WriteHeader("Access-Control-Allow-Origin", "*");
}

bool SAPI::Error(HTTPRequest* req, HTTPStatus::Codes status, const std::vector<SAPI::Result> &errors)
{
    UniValue arr(UniValue::VARR);

    for( auto error : errors ){
        arr.push_back(error.ToUniValue());
    }

    string strJSON = arr.write(1,1) + "\n";

    AddDefaultHeaders(req);
    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(status, strJSON);
    return false;
}

bool SAPI::Error(HTTPRequest* req, HTTPStatus::Codes status, const std::string &message)
{
    return SAPI::Error(req, status, std::vector<SAPI::Result>{SAPI::Result(SAPI::Undefined, message)});
}

bool SAPI::Error(HTTPRequest* req, HTTPStatus::Codes status, const SAPI::Result &error)
{
    return SAPI::Error(req, status, std::vector<SAPI::Result>{error});
}

bool SAPI::Error(HTTPRequest* req, SAPI::Codes code, const std::string &message)
{
    return SAPI::Error(req, HTTPStatus::BAD_REQUEST, std::vector<SAPI::Result>{SAPI::Result(code, message)});
}

void SAPI::WriteReply(HTTPRequest *req, HTTPStatus::Codes status, const UniValue &obj)
{
    AddDefaultHeaders(req);
    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(status, JsonString(obj));
}

void SAPI::WriteReply(HTTPRequest *req, HTTPStatus::Codes status, const std::string &str)
{
    AddDefaultHeaders(req);
    req->WriteHeader("Content-Type", "text/plain");
    req->WriteReply(status, str + "\n");
}

void SAPI::WriteReply(HTTPRequest *req, const UniValue &obj)
{
    SAPI::WriteReply(req, HTTPStatus::OK, obj);
}

void SAPI::WriteReply(HTTPRequest *req, const std::string &str)
{
    SAPI::WriteReply(req, HTTPStatus::OK, str);
}

int64_t SAPI::GetStartTime() {
    return nStartTime;
}

CSAPIStatistics::CSAPIStatistics()
{
    nTotalValidRequests = 0;
    nTotalInvalidRequests = 0;
    nTotalBlockedRequests = 0;

    nMaxRequestsPerHour = 0;
    nMaxClientsPerHour = 0;

    init();
}

void CSAPIStatistics::init()
{
    setCurrentClients.clear();

    vecRequests.clear();
    vecRequests.resize(nCountLastHours);

    nLastHour = GetCurrentHour();
    vecRequests[nLastHour].Reset();
    vecRequests[nLastHour].nStartTimestamp = GetCurrentStartTimestamp();

    int64_t nNextTimestamp;
    int64_t nNextHour = 0, nPrevHour = nLastHour;
    for( int i=1;i<nCountLastHours;i++){
        nNextHour = nPrevHour - 1;
        if( nNextHour < 0 ) nNextHour = nCountLastHours -1;
        nNextTimestamp = vecRequests[nPrevHour].nStartTimestamp - nSecondsPerHour;
        vecRequests[nNextHour].Reset();
        vecRequests[nNextHour].nStartTimestamp = nNextTimestamp;
        nPrevHour = nNextHour;
    }
}

void CSAPIStatistics::request(CNetAddr &address, RequestType type)
{
    LOCK(cs_requests);

    int nCurrentHour = GetCurrentHour();

    if( (GetTime() - vecRequests[nLastHour].nStartTimestamp) > (nCountLastHours * nSecondsPerHour) ){
        init();
    }else{

        while( nLastHour != nCurrentHour ){
            int64_t nNextTimestamp = vecRequests[nLastHour].nStartTimestamp + nSecondsPerHour;
            nLastHour++;
            if( nLastHour >= nCountLastHours) nLastHour = 0;

            vecRequests[nLastHour].Reset();
            vecRequests[nLastHour].nStartTimestamp = nNextTimestamp;
            setCurrentClients.clear();
        }
    }

    setCurrentClients.insert(address);

    uint64_t nClients = setCurrentClients.size();
    vecRequests[nCurrentHour].nClients = nClients;

    if( nClients > nMaxClientsPerHour ) nMaxClientsPerHour = nClients;

    switch(type){
    case Valid:
        ++nTotalValidRequests;
        vecRequests[nCurrentHour].nValid++;
        break;
    case Invalid:
        ++nTotalInvalidRequests;
        vecRequests[nCurrentHour].nInvalid++;
        break;
    case Blocked:
        ++nTotalBlockedRequests;
        vecRequests[nCurrentHour].nBlocked++;
        break;
    default:
        break;
    }

    if( vecRequests[nCurrentHour].GetTotalRequests() > nMaxRequestsPerHour )
        nMaxRequestsPerHour = vecRequests[nCurrentHour].GetTotalRequests();

}

void CSAPIStatistics::reset()
{
    vecRestarts.push_back(GetTime());
}

int CSAPIStatistics::GetCurrentHour(){
    return (GetTime()/nSecondsPerHour) % nCountLastHours;
}

int CSAPIStatistics::GetCurrentStartTimestamp(){
    int64_t nCurrentTime = GetTime();
    return nCurrentTime - (nCurrentTime % nSecondsPerHour);
}

UniValue CSAPIStatistics::ToUniValue()
{
    LOCK(cs_requests);

    UniValue obj(UniValue::VOBJ);
    UniValue last24h(UniValue::VARR);

    obj.pushKV("totalValid", GetTotalValidRequests());
    obj.pushKV("totalInvalid", GetTotalInvalidRequests() );
    obj.pushKV("totalBlocked", GetTotalBlockedRequests() );
    obj.pushKV("maxRequestsPerHour", GetMaxRequestsPerHour() );
    obj.pushKV("maxClientsPerHour", GetMaxClientsPerHour() );

    int nIndex = nLastHour;

    for( int i=0;i<nCountLastHours;i++){

        CSAPIRequestCount& count = vecRequests[nIndex];

        UniValue hour(UniValue::VOBJ);

        hour.pushKV("timestamp", count.nStartTimestamp);
        hour.pushKV("clients", count.nClients);
        hour.pushKV("valid", count.nValid);
        hour.pushKV("invalid", count.nInvalid);
        hour.pushKV("blocked", count.nBlocked);

        last24h.push_back(hour);

        --nIndex;
        if( nIndex < 0 ) nIndex = nCountLastHours - 1;
    }

    obj.pushKV("last24Hours", last24h );
    obj.pushKV("restarts", static_cast<int64_t>(vecRestarts.size()));

    return obj;
}

string CSAPIStatistics::ToString() const
{
    return strprintf("CSAPIStatistics( restarts=%d, totalValidRequests=%d )", vecRestarts.size(), nTotalValidRequests);
}
