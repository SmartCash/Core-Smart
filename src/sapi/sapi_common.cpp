// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sapi.h"

#include <algorithm>
#include "base58.h"
#include "rpc/client.h"
#include "sapi_validation.h"
#include "sapi/sapi_common.h"
#include "validation.h"
#include "clientversion.h"

static bool client_status(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);

SAPI::EndpointGroup clientEndpoints = {
    "client",
    {
        {
            "status", HTTPRequest::GET, UniValue::VNULL, client_status,
            {
                // No body parameter
            }
        }
    }
};

static bool client_status(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    UniValue response(UniValue::VOBJ);
    UniValue connections(UniValue::VOBJ);
    UniValue sapi(UniValue::VOBJ);

    response.pushKV("started", SAPI::GetStartTime());
    response.pushKV("uptime", GetTime() - SAPI::GetStartTime());
    response.pushKV("version", CLIENT_VERSION);
    response.pushKV("protocolversion", PROTOCOL_VERSION);
    response.pushKV("blocks",        (int)chainActive.Height());
    response.pushKV("time",    GetTime());
    response.pushKV("timeoffset",    GetTimeOffset());

    connections.pushKV("in", (int)g_connman->GetNodeCount(CConnman::CONNECTIONS_IN));
    connections.pushKV("out", (int)g_connman->GetNodeCount(CConnman::CONNECTIONS_OUT));

    response.pushKV("connections",   connections);
    response.pushKV("requests", sapiStatistics.ToUniValue());
    response.pushKV("testnet",       Params().TestnetToBeDeprecatedFieldRPC());

    SAPI::WriteReply(req, response);

    return true;
}
