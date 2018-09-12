// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_SAPISERVER_H
#define SMARTCASH_SAPISERVER_H

#include "httpserver.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include <string>
#include <stdint.h>
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/function.hpp>

static const int DEFAULT_SAPI_THREADS=4;
static const int DEFAULT_SAPI_WORKQUEUE=16;
static const int DEFAULT_SAPI_SERVER_TIMEOUT=30;
static const int DEFAULT_SAPI_SERVER_PORT=9680;

static const int DEFAULT_SAPI_JSON_INDENT=2;

/** Initialize SAPI server. */
bool InitSAPIServer();
/** Start SAPI server. */
bool StartSAPIServer();
/** Interrupt SAPI server threads */
void InterruptSAPIServer();
/** Stop SAPI server */
void StopSAPIServer();

/** Register handler for prefix.
 * If multiple handlers match a prefix, the first-registered one will
 * be invoked.
 */
void RegisterSAPIHandler(const std::string &prefix, bool exactMatch, const HTTPRequestHandler &handler);
/** Unregister handler for prefix */
void UnregisterSAPIHandler(const std::string &prefix, bool exactMatch);


#endif // SMARTCASH_SAPISERVER_H
