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
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();
    }

    if (fHelp  ||
        (
         strCommand != "current" && strCommand != "history" && strCommand != "check" && strCommand != "payouts"))
            throw std::runtime_error(
                "smartrewards \"command\"...\n"
                "Set of commands to execute smartreward related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
                "  current      - Print information about the current SmartReward cycle.\n"
                "  history      - Print the results of all past SmartReward cycles.\n"
                "  payouts      - Print a list of all paid rewards in a past cycle.\n"
                "  check        - Check a SmartCash address for eligibility in the current rewards cycle.\n"
                );

    if (strCommand == "current")
    {
        UniValue obj(UniValue::VOBJ);

        CSmartRewardsRound current;
        if( !prewards->GetCurrentRound(current) )
            throw JSONRPCError(RPC_DATABASE_ERROR, "Couldn't read from the rewards database.");

        obj.push_back(Pair("rewards_cycle",current.number));
        obj.push_back(Pair("start_blockheight",current.startBlockHeight));
        obj.push_back(Pair("start_blocktime",current.startBlockTime));
        obj.push_back(Pair("end_blockheight",current.endBlockHeight));
        obj.push_back(Pair("end_blocktime",current.endBlockTime));
        obj.push_back(Pair("eligible_addresses",current.eligibleEntries));
        obj.push_back(Pair("eligible_smart",current.eligibleSmart));
        obj.push_back(Pair("estimated_percent",current.percent));

        return obj;
    }

    if (strCommand == "history")
    {
        UniValue obj(UniValue::VARR);

        std::vector<CSmartRewardsRound> history;

        if( !prewards->GetRewardRounds(history))
            throw JSONRPCError(RPC_DATABASE_ERROR, "Couldn't read from the rewards database.");

        BOOST_FOREACH(CSmartRewardsRound round, history) {

            UniValue roundObj(UniValue::VOBJ);

            roundObj.push_back(Pair("rewards_cycle",round.number));
            roundObj.push_back(Pair("start_blockheight",round.startBlockHeight));
            roundObj.push_back(Pair("start_blocktime",round.startBlockTime));
            roundObj.push_back(Pair("end_blockheight",round.endBlockHeight));
            roundObj.push_back(Pair("end_blocktime",round.endBlockTime));
            roundObj.push_back(Pair("eligible_addresses",round.eligibleEntries));
            roundObj.push_back(Pair("eligible_smart",round.eligibleSmart));
            roundObj.push_back(Pair("percent",round.percent));

            obj.push_back(roundObj);
        }

        return obj;
    }

    if(strCommand == "payouts")
    {
        CSmartRewardsRound current;
        if( !prewards->GetCurrentRound(current) )
            throw JSONRPCError(RPC_DATABASE_ERROR, "Couldn't read from the rewards database.");

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

        std::vector<CSmartRewardPayout> vect;

        if( !prewards->GetRewardPayouts(round,vect) )
            throw JSONRPCError(RPC_DATABASE_ERROR, "Couldn't fetch the list from the database.");

        UniValue obj(UniValue::VOBJ);

        BOOST_FOREACH(CSmartRewardPayout payout, vect) {

            txnouttype type;
            vector<CTxDestination> addresses;
            int nRequired;

            ExtractDestinations(payout.pubKey,type,addresses,nRequired);

            std::string addrString = "";
            BOOST_FOREACH(CTxDestination address, addresses) {
                if( addrString != "" ) addrString += ",";
                addrString += CBitcoinAddress(address).ToString();
            }

            UniValue addrObj(UniValue::VOBJ);

            addrObj.push_back(Pair("balance", payout.balance));
            addrObj.push_back(Pair("reward", payout.reward));

            obj.push_back(Pair(addrString, addrObj));

        }

        return obj;
    }

    if (strCommand == "check")
    {
        std::string addressString = params[1].get_str();
        CBitcoinAddress address = CBitcoinAddress(addressString);

        if( !address.IsValid() ) throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Invalid SMART address provided: %s",addressString));

        CScript pubKeyScript = GetScriptForDestination(address.Get());
        CSmartRewardEntry entry;

        if( !prewards->GetRewardEntry(pubKeyScript, entry) ) throw JSONRPCError(RPC_DATABASE_ERROR, "Couldn't find this SMART address in the databse!");

        UniValue obj(UniValue::VOBJ);

        std::function<double (CAmount)> format = [](CAmount a) {
            return a / COIN + ( double(a % COIN) / COIN );
        };

        obj.push_back(Pair("address",addressString));
        obj.push_back(Pair("balance",format(entry.balance)));
        obj.push_back(Pair("balance_eligible", format(entry.eligible ? entry.balanceOnStart : 0)));

        return obj;
    }

    return NullUniValue;
}
