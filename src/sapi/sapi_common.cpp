// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sapi.h"

#include <algorithm>
#include "base58.h"
#include "rpc/client.h"
#include "sapi_validation.h"
#include "sapi/sapi_common.h"
#include "smartnode/instantx.h"
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
            },
        }
    }
};

static bool statistic_requests(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool statistic_instantpay(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool statistic_instantpay_list(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);

SAPI::EndpointGroup statisticEndpoints = {
    "statistic",
    {
        {
            "requests", HTTPRequest::GET, UniValue::VNULL, statistic_requests,
            {
                // No body parameter
            },
        },
        {
            "instantpay", HTTPRequest::GET, UniValue::VNULL, statistic_instantpay,
            {
                // No body parameter
            },
        },
        {
            "instantpay", HTTPRequest::POST, UniValue::VOBJ, statistic_instantpay_list,
            {
                SAPI::BodyParameter(SAPI::Keys::timestampFrom,  new SAPI::Validation::UInt(), true),
                SAPI::BodyParameter(SAPI::Keys::timestampTo,    new SAPI::Validation::UInt(), true),
                SAPI::BodyParameter(SAPI::Keys::pageNumber,     new SAPI::Validation::IntRange(1,INT_MAX)),
                SAPI::BodyParameter(SAPI::Keys::pageSize,       new SAPI::Validation::IntRange(1,1000)),
                SAPI::BodyParameter(SAPI::Keys::ascending,      new SAPI::Validation::Bool(), true),
            }
        }
    }
};

static bool client_status(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    UniValue response(UniValue::VOBJ);
    UniValue connections(UniValue::VOBJ);

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
    response.pushKV("testnet",       Params().TestnetToBeDeprecatedFieldRPC());

    SAPI::WriteReply(req, response);

    return true;
}

static bool statistic_requests(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    SAPI::WriteReply(req, sapiStatistics.ToUniValue());
    return true;
}

static bool statistic_instantpay(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    int nInstantPayLocks = 0;
    int nStart = GetTime() - (24 * 60 * 60);
    int nEnd = GetTime();
    int nFirstTimestamp, nLastTimestamp;

    std::vector<std::pair<CInstantPayIndexKey, CInstantPayValue> > instantPayIndex;

    if (!GetInstantPayIndexCount(nInstantPayLocks, nFirstTimestamp, nLastTimestamp, nStart, nEnd) || !nInstantPayLocks)
        return SAPI::Error(req, SAPI::NoInstantPayLocksAvailble, "No InstantPay locks available for the last 24h.");

    if (!GetInstantPayIndex(instantPayIndex, nFirstTimestamp, 0 , nInstantPayLocks, false))
        return SAPI::Error(req, SAPI::NoInstantPayLocksAvailble, "No InstantPay locks available for the last 24h.");

    UniValue response(UniValue::VOBJ);

    int nSuccessful = 0, nFailed = 0, nTimeSum = 0;

    for (std::vector<std::pair<CInstantPayIndexKey, CInstantPayValue> >::const_iterator it=instantPayIndex.begin();
         it!=instantPayIndex.end();
         it++) {

        if( it->second.fValid ){
            ++nSuccessful;
        }else{
            ++nFailed;
        }

        nTimeSum += it->second.elapsedTime;
    }

    response.pushKV("successful", nSuccessful);
    response.pushKV("failed", nFailed);
    response.pushKV("meanElapsedTime", nTimeSum / (nSuccessful + nFailed));

    SAPI::WriteReply(req, response);

    return true;
}

static bool statistic_instantpay_list(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    int64_t nTime0, nTime1, nTime2, nTime3, nTime4, nTime5;

    nTime0 = GetTimeMicros();

    int64_t start = bodyParameter.exists(SAPI::Keys::timestampFrom) ? bodyParameter[SAPI::Keys::timestampFrom].get_int64() : 0;
    int64_t end = bodyParameter.exists(SAPI::Keys::timestampTo) ? bodyParameter[SAPI::Keys::timestampTo].get_int64() : INT_MAX;
    int64_t nPageNumber = bodyParameter[SAPI::Keys::pageNumber].get_int64();
    int64_t nPageSize = bodyParameter[SAPI::Keys::pageSize].get_int64();
    bool fAsc = bodyParameter.exists(SAPI::Keys::ascending) ? bodyParameter[SAPI::Keys::ascending].get_bool() : false;

    if ( end <= start)
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "\"" + SAPI::Keys::timestampFrom + "\" is expected to be greater than \"" + SAPI::Keys::timestampTo + "\"");

    int nInstantPayLocks = 0;
    int nFirstTimestamp;
    int nLastTimestamp;

    std::vector<std::pair<CInstantPayIndexKey, CInstantPayValue> > instantPayIndex;

    nTime1 = GetTimeMicros();

    if (!GetInstantPayIndexCount(nInstantPayLocks, nFirstTimestamp, nLastTimestamp, start, end) || !nInstantPayLocks)
        return SAPI::Error(req, SAPI::NoInstantPayLocksAvailble, "No InstantPay locks available for the given timerange.");

    int nPages = nInstantPayLocks / nPageSize;
    if( nInstantPayLocks % nPageSize ) nPages++;

    if (nPageNumber > nPages)
        return SAPI::Error(req, SAPI::PageOutOfRange, strprintf("Page number out of range: 1 - %d", nPages));

    int nIndexOffset = static_cast<int>(( nPageNumber - 1 ) * nPageSize);
    int nLimit = static_cast<int>((nInstantPayLocks % nPageSize) && nPageNumber == nPages ? (nInstantPayLocks % nPageSize) : nPageSize);

    nTime2 = GetTimeMicros();

    if (!GetInstantPayIndex(instantPayIndex, fAsc ? nFirstTimestamp : nLastTimestamp, nIndexOffset , nLimit, !fAsc))
        return SAPI::Error(req, SAPI::NoInstantPayLocksAvailble, "No information available");

    nTime3 = GetTimeMicros();

    UniValue arrLocks(UniValue::VARR);

    for (std::vector<std::pair<CInstantPayIndexKey, CInstantPayValue> >::const_iterator it=instantPayIndex.begin();
         it!=instantPayIndex.end();
         it++) {

        UniValue lockObj(UniValue::VOBJ);

        lockObj.pushKV("timestamp", static_cast<int64_t>(it->first.timestamp));
        lockObj.pushKV("txid", it->first.txhash.ToString());
        lockObj.pushKV("valid", it->second.fValid);
        lockObj.pushKV("receivedLocks", it->second.receivedLocks);
        lockObj.pushKV("maxLocks", it->second.maxLocks);
        lockObj.pushKV("elsapsedTime", it->second.elapsedTime);

        arrLocks.push_back(lockObj);
    }

    UniValue obj(UniValue::VOBJ);

    obj.pushKV("count", nInstantPayLocks);
    obj.pushKV("pages", nPages);
    obj.pushKV("page", nPageNumber);
    obj.pushKV("instantPayLocks",arrLocks);

    nTime4 = GetTimeMicros();

    SAPI::WriteReply(req, obj);

    nTime5 = GetTimeMicros();

    LogPrint("sapi-benchmark", "statistic_instantpay_list\n");
    LogPrint("sapi-benchmark", " Prepare parameter: %.2fms\n", (nTime1 - nTime0) * 0.001);
    LogPrint("sapi-benchmark", " Get instantpay count: %.2fms\n", (nTime2 - nTime1) * 0.001);
    LogPrint("sapi-benchmark", " Get instantpay index: %.2fms\n", (nTime3 - nTime2) * 0.001);
    LogPrint("sapi-benchmark", " Process instantpays: %.2fms\n", (nTime4 - nTime3) * 0.001);
    LogPrint("sapi-benchmark", " Write reply: %.2fms\n", (nTime5 - nTime4) * 0.001);
    LogPrint("sapi-benchmark", " Total: %.2fms\n\n", (nTime5 - nTime0) * 0.001);

    return true;
}
