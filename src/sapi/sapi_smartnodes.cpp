// Copyright (c) 2017 - 2020 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "sapi.h"

#include <algorithm>
#include "base58.h"
#include "rpc/client.h"
#include "sapi_validation.h"
#include "sapi/sapi_smartnodes.h"
#include "smartnode/smartnodeman.h"
#include "validation.h"

static bool smartnodes_count(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool smartnodes_roi(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool smartnodes_list(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool smartnodes_check_one(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool smartnodes_check_list(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool smartnodes_filter_list(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);

SAPI::EndpointGroup smartnodeEndpoints = {
    "smartnode",
    {
        {
            "count", HTTPRequest::GET, UniValue::VNULL, smartnodes_count,
            {
                // No body parameter
            }
        },
        {
            "list", HTTPRequest::GET, UniValue::VNULL, smartnodes_list,
            {
                // No body parameter
            }
        },
        {
            "check", HTTPRequest::POST, UniValue::VARR, smartnodes_check_list,
            {
               // No body parameter
            }
        },
        {
            "check/{info}", HTTPRequest::GET, UniValue::VNULL, smartnodes_check_one,
            {
               // No body parameter
            }
        },
        {
            "filter", HTTPRequest::POST, UniValue::VOBJ, smartnodes_filter_list,
            {
                SAPI::BodyParameter(SAPI::Keys::status, new SAPI::Validation::String(), true),
                SAPI::BodyParameter(SAPI::Keys::protocol, new SAPI::Validation::Int(), true)
            }
        },
        {
            "roi", HTTPRequest::GET, UniValue::VNULL, smartnodes_roi,
            {
                // No body parameter
            }
        },
    }
};

static bool CheckSmartnodes(HTTPRequest* req, std::vector<std::string> vecInfos, std::vector<UniValue> &vecResults)
{

    std::map<COutPoint, CSmartnode> mapSmartnodes = mnodeman.GetFullSmartnodeMap();

    for (auto& mnpair : mapSmartnodes) {
        CSmartnode mn = mnpair.second;
        std::string strOutpoint = strprintf("%s:%d", mnpair.first.hash.ToString(), mnpair.first.n);

        std::ostringstream streamFull;
        streamFull << std::setw(18) <<
                       mn.GetStatus() << " " <<
                       mn.nProtocolVersion << " " <<
                       CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString() << " " <<
                       (int64_t)mn.lastPing.sigTime << " " << std::setw(8) <<
                       (int64_t)(mn.lastPing.sigTime - mn.sigTime) << " " << std::setw(10) <<
                       mn.GetLastPaidTime() << " "  << std::setw(6) <<
                       mn.GetLastPaidBlock() << " " <<
                       mn.addr.ToString();
        std::string strFull = streamFull.str();

        for( auto info : vecInfos ){
            if (info !="" && strFull.find(info) == std::string::npos &&
                strOutpoint.find(info) == std::string::npos) continue;

            UniValue node(UniValue::VOBJ);

            node.pushKV("outpoint", strOutpoint);
            node.pushKV("status", mn.GetStatus());
            node.pushKV("protocol", mn.nProtocolVersion);
            node.pushKV("payee", CSmartAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            node.pushKV("lastSeen", mn.lastPing.sigTime);
            node.pushKV("uptime", mn.lastPing.sigTime - mn.sigTime);
            node.pushKV("lastPaidTime", mn.GetLastPaidTime());
            node.pushKV("lastPaidBlock", mn.GetLastPaidBlock());
            node.pushKV("ip", mn.addr.ToString());

            vecResults.push_back(node);
        }
    }

    if( !vecResults.size() ){
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "Failed to find a SmartNode for the given information");
    }

    return true;
}

static bool smartnodes_count(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    std::map<std::string,int64_t> mapStates;

    mnodeman.CountStates(mapStates);

    UniValue response(UniValue::VOBJ);

    mnodeman.CountStates(mapStates);

    for (auto& state : mapStates) {
        response.pushKV(state.first, state.second);
    }

    SAPI::WriteReply(req, response);

    return true;
}

static bool smartnodes_roi(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    LOCK(cs_main);
    UniValue response(UniValue::VOBJ);

    {
        response.pushKV("SmartNode Collateral", 100000 );
        response.pushKV("Yearly Rewards", (717500000 / chainActive.Height()) * (573775 / (1 + mnodeman.CountEnabled())) / 10 );
        response.pushKV("Yearly Yield %", (5000 + (717500000 / chainActive.Height()) * (573775 / (1 + mnodeman.CountEnabled()))) / 10000 );
    }

    SAPI::WriteReply(req, response);

    return true;
}

static bool smartnodes_list(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    UniValue obj(UniValue::VOBJ);

    std::map<COutPoint, CSmartnode> mapSmartnodes = mnodeman.GetFullSmartnodeMap();

    for (auto& mnpair : mapSmartnodes) {
        CSmartnode mn = mnpair.second;

        UniValue node(UniValue::VOBJ);

        node.pushKV("status", mn.GetStatus());
        node.pushKV("protocol", mn.nProtocolVersion);
        node.pushKV("payee", CSmartAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
        node.pushKV("lastSeen", mn.lastPing.sigTime);
        node.pushKV("uptime", mn.lastPing.sigTime - mn.sigTime);
        node.pushKV("lastPaidTime", mn.GetLastPaidTime());
        node.pushKV("lastPaidBlock", mn.GetLastPaidBlock());
        node.pushKV("ip", mn.addr.ToString());

        obj.pushKV(strprintf("%s:%d", mnpair.first.hash.ToString(), mnpair.first.n), node);
    }

    SAPI::WriteReply(req, obj);

    return true;
}

static bool smartnodes_check_one(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    if ( !mapPathParams.count("info") )
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "No SmartCash address specified. Use /smartnode/check/<smartnode_info>");

    std::string strInfo = mapPathParams.at("info");

    std::vector<UniValue> vecResults;

    if( !CheckSmartnodes(req, {strInfo}, vecResults) ) return false;

    UniValue obj(UniValue::VARR);

    for( auto result : vecResults ) obj.push_back(result);

    SAPI::WriteReply(req, obj);

    return true;
}

static bool smartnodes_check_list(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    if( !bodyParameter.isArray() || bodyParameter.empty() )
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "Addresses are expected to be a JSON array: [ \"address\", ... ]");

    std::vector<UniValue> vecResults;
    std::vector<std::string> vecInfos;

    for( auto info : bodyParameter.getValues() ){

        std::string strInfo = info.get_str();

        if( std::find(vecInfos.begin(), vecInfos.end(), strInfo) == vecInfos.end() )
            vecInfos.push_back(strInfo);
    }

    if( !CheckSmartnodes(req, vecInfos, vecResults) ) return false;

    UniValue obj(UniValue::VARR);

    for( auto result : vecResults ) obj.push_back(result);

    SAPI::WriteReply(req, obj);

    return true;
}

static bool smartnodes_filter_list(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    std::string filterStatus = bodyParameter.exists(SAPI::Keys::status) ? bodyParameter[SAPI::Keys::status].get_str() : "*";
    int64_t filterProtocol = bodyParameter.exists(SAPI::Keys::protocol) ? bodyParameter[SAPI::Keys::protocol].get_int64() : -1;

    UniValue result(UniValue::VARR);

    std::map<COutPoint, CSmartnode> mapSmartnodes = mnodeman.GetFullSmartnodeMap();

    for (auto& mnpair : mapSmartnodes) {
        CSmartnode mn = mnpair.second;
        if (((filterStatus == "*") || (mn.GetStatus() == filterStatus)) &&
            ((filterProtocol < 0) || (mn.nProtocolVersion == filterProtocol))) {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("payee", CSmartAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            obj.pushKV("ip", mn.addr.ToString());
            result.push_back(obj);
        }
    }

    SAPI::WriteReply(req, result);

    return true;
}
