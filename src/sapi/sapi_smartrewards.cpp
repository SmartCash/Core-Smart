// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "sapi.h"

#include <algorithm>
#include "base58.h"
#include "rpc/client.h"
#include "sapi_validation.h"
#include "sapi/sapi_smartrewards.h"
#include "smartrewards/rewards.h"
#include "smartrewards/rewardspayments.h"
#include "validation.h"

static bool smartrewards_current(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool smartrewards_history(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool smartrewards_check_one(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool smartrewards_check_list(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);

SAPI::EndpointGroup smartrewardsEndpoints = {
    "smartrewards",
    {
        {
            "current", HTTPRequest::GET, UniValue::VNULL, smartrewards_current,
            {
                // No body parameter
            }
        },
        {
            "history", HTTPRequest::GET, UniValue::VNULL, smartrewards_history,
            {
                // No body parameter
            }
        },
        {
            "check", HTTPRequest::POST, UniValue::VARR, smartrewards_check_list,
            {
               // No body parameter
            }
        },
        {
            "check/{address}", HTTPRequest::GET, UniValue::VNULL, smartrewards_check_one,
            {
               // No body parameter
            }
        }
    }
};

static bool CheckAddresses(HTTPRequest* req, std::vector<std::string> vecAddr, std::vector<UniValue> &vecResults)
{
    SAPI::Codes code = SAPI::Valid;
    std::string error = std::string();
    std::vector<SAPI::Result> errors;

    vecResults.clear();

    TRY_LOCK(cs_rewardrounds,roundsLocked);

    if(!roundsLocked) return SAPI::Error(req, SAPI::RewardsDatabaseBusy, "Rewards database is busy..Try it again!");

    const CSmartRewardRound& current = prewards->GetCurrentRound();

    int nFirst_1_3_Round = Params().GetConsensus().nRewardsFirst_1_3_Round;

    for( auto addrStr : vecAddr ){

        CSmartAddress id = CSmartAddress(addrStr);

        if( !id.IsValid() ){
            code = SAPI::InvalidSmartCashAddress;
            std::string message = "Invalid address: " + addrStr;
            errors.push_back(SAPI::Result(code, message));
            continue;
        }

        CSmartRewardEntry entry;

        if( !prewards->GetRewardEntry(id, entry) ){
            code = SAPI::AddressNotFound;
            std::string message = "Couldn't find this SmartCash address in the database.";
            errors.push_back(SAPI::Result(code, message));
            continue;
        }

        UniValue obj(UniValue::VOBJ);

        obj.pushKV("address",id.ToString());
        obj.pushKV("balance",UniValueFromAmount(entry.balance));
        obj.pushKV("balance_eligible", UniValueFromAmount(entry.balanceEligible));
        obj.pushKV("is_smartnode", entry.fIsSmartNode);
        obj.pushKV("voted", !entry.voteProof.IsNull());
        obj.pushKV("eligible", current.number < nFirst_1_3_Round ? entry.balanceEligible > 0 : entry.IsEligible());

        vecResults.push_back(obj);
    }

    if( errors.size() ){
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, errors);
    }

    if( !vecResults.size() ){
        return SAPI::Error(req, HTTPStatus::INTERNAL_SERVER_ERROR, "Balance check failed unexpected.");
    }

    return true;
}

static bool smartrewards_current(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    UniValue obj(UniValue::VOBJ);

    TRY_LOCK(cs_rewardrounds,roundsLocked);

    if(!roundsLocked) return SAPI::Error(req, SAPI::RewardsDatabaseBusy, "Rewards database is busy..Try it again!");

    const CSmartRewardRound& current = prewards->GetCurrentRound();

    if( !current.number ) return SAPI::Error(req, SAPI::NoActiveRewardRound, "No active reward round available yet.");

    obj.pushKV("rewards_cycle",current.number);
    obj.pushKV("start_blockheight",current.startBlockHeight);
    obj.pushKV("start_blocktime",current.startBlockTime);
    obj.pushKV("end_blockheight",current.endBlockHeight);
    obj.pushKV("end_blocktime",current.endBlockTime);
    obj.pushKV("eligible_addresses",current.eligibleEntries - current.disqualifiedEntries);
    obj.pushKV("eligible_smart",UniValueFromAmount(current.eligibleSmart - current.disqualifiedSmart));
    obj.pushKV("disqualified_addresses",current.disqualifiedEntries);
    obj.pushKV("disqualified_smart",UniValueFromAmount(current.disqualifiedSmart));
    obj.pushKV("estimated_rewards",UniValueFromAmount(current.rewards));
    obj.pushKV("estimated_percent",current.percent);

    SAPI::WriteReply(req, obj);

    return true;
}


static bool smartrewards_history(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    UniValue obj(UniValue::VARR);

    TRY_LOCK(cs_rewardrounds,roundsLocked);

    if(!roundsLocked) return SAPI::Error(req, SAPI::RewardsDatabaseBusy, "Rewards database is busy..Try it again!");

    const CSmartRewardRoundList& history = prewards->GetRewardRounds();

    int64_t nPayoutDelay = Params().GetConsensus().nRewardsPayoutStartDelay;

    if(!history.size()) return SAPI::Error(req, SAPI::NoFinishedRewardRound, "No finished reward round available yet.");

    BOOST_FOREACH(CSmartRewardRound round, history) {

        UniValue roundObj(UniValue::VOBJ);

        roundObj.pushKV("rewards_cycle",round.number);
        roundObj.pushKV("start_blockheight",round.startBlockHeight);
        roundObj.pushKV("start_blocktime",round.startBlockTime);
        roundObj.pushKV("end_blockheight",round.endBlockHeight);
        roundObj.pushKV("end_blocktime",round.endBlockTime);
        roundObj.pushKV("eligible_addresses",round.eligibleEntries - round.disqualifiedEntries);
        roundObj.pushKV("eligible_smart",UniValueFromAmount(round.eligibleSmart - round.disqualifiedSmart));
        roundObj.pushKV("disqualified_addresses",round.disqualifiedEntries);
        roundObj.pushKV("disqualified_smart",UniValueFromAmount(round.disqualifiedSmart));
        roundObj.pushKV("rewards",UniValueFromAmount(round.rewards));
        roundObj.pushKV("percent",round.percent);

        UniValue payObj(UniValue::VOBJ);

        int nPayeeCount = round.eligibleEntries - round.disqualifiedEntries;
        int nBlockPayees = round.nBlockPayees;
        int nPayoutInterval = round.nBlockInterval;
        int nRewardBlocks = nPayeeCount / nBlockPayees;
        if( nPayeeCount % nBlockPayees ) nRewardBlocks += 1;
        int nLastRoundBlock = round.endBlockHeight + nPayoutDelay + ( (nRewardBlocks - 1) * nPayoutInterval );

        payObj.pushKV("firstBlock", round.endBlockHeight + nPayoutDelay );
        payObj.pushKV("totalBlocks", nRewardBlocks );
        payObj.pushKV("lastBlock", nLastRoundBlock );
        payObj.pushKV("totalPayees", nPayeeCount);
        payObj.pushKV("blockPayees", round.nBlockPayees);
        payObj.pushKV("lastBlockPayees", nPayeeCount % nBlockPayees );
        payObj.pushKV("blockInterval",round.nBlockInterval);

        roundObj.pushKV("payouts", payObj);

        obj.push_back(roundObj);
    }

    SAPI::WriteReply(req, obj);

    return true;
}

static bool smartrewards_check_one(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{

    if ( !mapPathParams.count("address") )
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "No SmartCash address specified. Use /smartrewards/check/<smartcash_address>");

    std::string addrStr = mapPathParams.at("address");
    std::vector<UniValue> vecResults;

    if( !CheckAddresses(req, {addrStr}, vecResults) ) return false;

    SAPI::WriteReply(req, vecResults[0]);

    return true;
}

static bool smartrewards_check_list(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{

    if( !bodyParameter.isArray() || bodyParameter.empty() )
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "Addresses are expedted to be a JSON array: [ \"address\", ... ]");

    std::vector<UniValue> vecResults;
    std::vector<std::string> vecAddresses;

    for( auto addr : bodyParameter.getValues() ){

        std::string addrStr = addr.get_str();

        if( std::find(vecAddresses.begin(), vecAddresses.end(), addrStr) == vecAddresses.end() )
            vecAddresses.push_back(addrStr);
    }

    if( !CheckAddresses(req, vecAddresses, vecResults) ) return false;

    UniValue obj(UniValue::VARR);

    for( auto result : vecResults ) obj.push_back(result);

    SAPI::WriteReply(req, obj);

    return true;
}
