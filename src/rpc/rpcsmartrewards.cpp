// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "init.h"
#include "validation.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "smartrewards/rewards.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet/wallet.h"

#include <fstream>
#include <iomanip>
#include <univalue.h>


UniValue smartrewards(const UniValue& params, bool fHelp)
{
    std::function<double (CAmount)> format = [](CAmount a) {
        return a / COIN + ( double(a % COIN) / COIN );
    };

    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();
    }

    if (fHelp  ||
        (
         strCommand != "current" && strCommand != "snapshot" && strCommand != "history" && strCommand != "check" && strCommand != "payouts"))
            throw std::runtime_error(
                "smartrewards \"command\"...\n"
                "Set of commands to execute smartreward related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
                "  current           - Print information about the current SmartReward cycle.\n"
                "  history           - Print the results of all past SmartReward cycles.\n"
                "  payouts  :round   - Print a list of all paid rewards in the past cycle :round\n"
                "  snapshot :round   - Print a list of all addresses with their balances from the end of the past cycle :round.\n"
                "  check :address    - Check the given :address for eligibility in the current rewards cycle.\n"
                );

    if( !fDebug && !prewards->IsSynced() )
        throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Rewards database is not up to date. Current progress %d%%",int(prewards->GetProgress() * 100)));

    TRY_LOCK(cs_rewardsdb, lockRewardsDb);

    if (!lockRewardsDb) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Rewards database is busy..Try it again!");
    }

    if (strCommand == "current")
    {
        UniValue obj(UniValue::VOBJ);

        TRY_LOCK(cs_rewardrounds,roundsLocked);

        if(!roundsLocked) throw JSONRPCError(RPC_DATABASE_ERROR, "Rewards database is busy..Try it again!");

        const CSmartRewardRound& current = prewards->GetCurrentRound();

        if( !current.number ) throw JSONRPCError(RPC_DATABASE_ERROR, "No active reward round available yet.");

        obj.push_back(Pair("rewards_cycle",current.number));
        obj.push_back(Pair("start_blockheight",current.startBlockHeight));
        obj.push_back(Pair("start_blocktime",current.startBlockTime));
        obj.push_back(Pair("end_blockheight",current.endBlockHeight));
        obj.push_back(Pair("end_blocktime",current.endBlockTime));
        obj.push_back(Pair("eligible_addresses",current.eligibleEntries - current.disqualifiedEntries));
        obj.push_back(Pair("eligible_smart",format(current.eligibleSmart - current.disqualifiedSmart)));
        obj.push_back(Pair("disqualified_addresses",current.disqualifiedEntries));
        obj.push_back(Pair("disqualified_smart",format(current.disqualifiedSmart)));
        obj.push_back(Pair("estimated_rewards",format(current.rewards)));
        obj.push_back(Pair("estimated_percent",current.percent));

        return obj;
    }

    if (strCommand == "history")
    {
        UniValue obj(UniValue::VARR);

        TRY_LOCK(cs_rewardrounds,roundsLocked);

        if(!roundsLocked) throw JSONRPCError(RPC_DATABASE_ERROR, "Rewards database is busy..Try it again!");

        const CSmartRewardRoundList& history = prewards->GetRewardRounds();

        if(!history.size()) throw JSONRPCError(RPC_DATABASE_ERROR, "No finished reward round available yet.");

        BOOST_FOREACH(CSmartRewardRound round, history) {

            UniValue roundObj(UniValue::VOBJ);

            roundObj.push_back(Pair("rewards_cycle",round.number));
            roundObj.push_back(Pair("start_blockheight",round.startBlockHeight));
            roundObj.push_back(Pair("start_blocktime",round.startBlockTime));
            roundObj.push_back(Pair("end_blockheight",round.endBlockHeight));
            roundObj.push_back(Pair("end_blocktime",round.endBlockTime));
            roundObj.push_back(Pair("eligible_addresses",round.eligibleEntries - round.disqualifiedEntries));
            roundObj.push_back(Pair("eligible_smart",format(round.eligibleSmart - round.disqualifiedSmart)));
            roundObj.push_back(Pair("disqualified_addresses",round.disqualifiedEntries));
            roundObj.push_back(Pair("disqualified_smart",format(round.disqualifiedSmart)));
            roundObj.push_back(Pair("rewards",format(round.rewards)));
            roundObj.push_back(Pair("percent",round.percent));

            obj.push_back(roundObj);
        }

        return obj;
    }

    if(strCommand == "payouts")
    {
        TRY_LOCK(cs_rewardrounds,roundsLocked);

        if(!roundsLocked) throw JSONRPCError(RPC_DATABASE_ERROR, "Rewards database is busy..Try it again!");

        const CSmartRewardRound& current = prewards->GetCurrentRound();

        if( !current.number ) throw JSONRPCError(RPC_DATABASE_ERROR, "No active reward round available yet.");

        int round = 0;
        std::string err = strprintf("Past SmartReward round required: 1 - %d ",current.number - 1 );

        if (params.size() != 2) throw JSONRPCError(RPC_INVALID_PARAMETER, err);

        try {
             int n = std::stoi(params[1].get_str());
             round = n;
        }
        catch (const std::invalid_argument& ia) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, err);
        }
        catch (const std::out_of_range& oor) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, err);
        }
        catch (...) {}

        if(round < 1 || round >= current.number) throw JSONRPCError(RPC_INVALID_PARAMETER, err);

        CSmartRewardSnapshotList payouts;

        if( !prewards->GetRewardPayouts(round,payouts) )
            throw JSONRPCError(RPC_DATABASE_ERROR, "Couldn't fetch the list from the database.");

        UniValue obj(UniValue::VARR);

        BOOST_FOREACH(CSmartRewardSnapshot payout, payouts) {

            UniValue addrObj(UniValue::VOBJ);
            addrObj.push_back(Pair("address", payout.id.ToString()));
            addrObj.push_back(Pair("reward", format(payout.reward)));

            obj.push_back(addrObj);
        }

        return obj;
    }

    if(strCommand == "snapshot")
    {
        TRY_LOCK(cs_rewardrounds,roundsLocked);

        if(!roundsLocked) throw JSONRPCError(RPC_DATABASE_ERROR, "Rewards database is busy..Try it again!");

        const CSmartRewardRound& current = prewards->GetCurrentRound();

        if( !current.number ) throw JSONRPCError(RPC_DATABASE_ERROR, "No active reward round available yet.");

        int round = 0;
        std::string err = strprintf("Past SmartReward round required: 1 - %d ",current.number - 1 );

        if (params.size() != 2) throw JSONRPCError(RPC_INVALID_PARAMETER, err);

        try {
             int n = std::stoi(params[1].get_str());
             round = n;
        }
        catch (const std::invalid_argument& ia) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, err);
        }
        catch (const std::out_of_range& oor) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, err);
        }
        catch (...) {}

        if(round < 1 || round >= current.number) throw JSONRPCError(RPC_INVALID_PARAMETER, err);

        CSmartRewardSnapshotList snapshot;

        if( !prewards->GetRewardSnapshots(round,snapshot) )
            throw JSONRPCError(RPC_DATABASE_ERROR, "Couldn't fetch the list from the database.");

        UniValue obj(UniValue::VARR);

        BOOST_FOREACH(CSmartRewardSnapshot s, snapshot) {

            UniValue addrObj(UniValue::VOBJ);
            addrObj.push_back(Pair("address", s.id.ToString()));
            addrObj.push_back(Pair("balance", format(s.balance)));

            obj.push_back(addrObj);
        }

        return obj;
    }

    if (strCommand == "check")
    {
        if (params.size() != 2) throw JSONRPCError(RPC_INVALID_PARAMETER, "SMART address required.");

        std::string addressString = params[1].get_str();
        CSmartAddress id = CSmartAddress(addressString);

        if( !id.IsValid() ) throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Invalid SMART address provided: %s",addressString));

        CSmartRewardEntry entry;

        if( !prewards->GetRewardEntry(id, entry) ) throw JSONRPCError(RPC_DATABASE_ERROR, "Couldn't find this SMART address in the databse!");

        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("address",id.ToString()));
        obj.push_back(Pair("balance",format(entry.balance)));
        obj.push_back(Pair("balance_eligible", format(entry.eligible ? entry.balanceOnStart : 0)));

        return obj;
    }

    return NullUniValue;
}
