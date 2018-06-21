// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "init.h"
#include "validation.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "smartnode/spork.h"
#include "smartmining/miningpayments.h"
#include "smarthive/hive.h"
#include "util.h"
#include "wallet/wallet.h"
#include <univalue.h>

UniValue smartmining(const UniValue& params, bool fHelp)
{

    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();
    }

    if (fHelp  ||
        (
         strCommand != "status" && strCommand != "keys" && strCommand != "blocks" && strCommand != "disable" && strCommand != "enable" && strCommand != "warnings"))
            throw std::runtime_error(
                "smartmining \"command\"...\n"
                "Set of commands to execute smartmining related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
                "  status                - Print the current status of the enforcement and the keys.\n"
                "  blocks :count         - Print a list of the keys used in the latest :count blocks.\n"
                "  warnings :count       - Check the last :count blocks for strange abnormalities.\n"
                );

    if (strCommand == "status")
    {
        TRY_LOCK(cs_miningkeys,keysLocked);

        if(!keysLocked) throw JSONRPCError(RPC_DATABASE_ERROR, "Mining keys locked..Try it again!");

        auto *pKeyMap = &mapMiningKeysMainnet;

        if( TestNet() ) pKeyMap = &mapMiningKeysTestnet;

        int64_t nEnforcementState = sporkManager.GetSporkValue(SPORK_16_MINING_SIGNATURE_ENFORCEMENT);
        int64_t nKeyStates = sporkManager.GetSporkValue(SPORK_17_MINING_SIGNATURE_PUBKEYS_ENABLED);

        UniValue obj(UniValue::VOBJ);
        UniValue objKeys(UniValue::VOBJ);

        auto it = pKeyMap->begin();

        while (it != pKeyMap->end()){

            UniValue objKey(UniValue::VOBJ);

            bool isEnabled = ( nKeyStates & (1 << it->first) ) == (1 << it->first);

            objKey.pushKV("status", isEnabled ? "enabled" : "disabled");
            objKey.pushKV("address", it->second.ToString());

            objKeys.pushKV(std::to_string(it->first), objKey);

            it++;
        }

        bool fEnabled = nEnforcementState != 4070908800ULL;

        obj.pushKV("status", fEnabled ? "enabled" : "disabled" );

        if( fEnabled ) obj.pushKV("startHeight", nEnforcementState);
        obj.pushKV("keys", objKeys);

        return obj;
    }

    if (strCommand == "enable")
    {
#ifdef ENABLE_WALLET

        if (params.size() == 2){

            int64_t nBlockHeight = params[1].get_int64();

            if (!g_connman)
                throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

            //broadcast new spork
            if(sporkManager.UpdateSpork(SPORK_16_MINING_SIGNATURE_ENFORCEMENT, nBlockHeight, *g_connman)){
                return "success";
            } else {
                return "failure";
            }

        }

    throw runtime_error(
        "smartmining enable [<blockHeight>]\n"
        "<blockHeight> is the height the signatures start to become required.\n"
        + HelpRequiringPassphrase());
#else // ENABLE_WALLET
    throw runtime_error("No wallet support!");
#endif // ENABLE_WALLET

    }

    if(strCommand == "disable")
    {
#ifdef ENABLE_WALLET

        if (params.size() == 1){

            if (!g_connman)
                throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

            //broadcast new spork
            if(sporkManager.UpdateSpork(SPORK_16_MINING_SIGNATURE_ENFORCEMENT, 4070908800ULL, *g_connman)){
                return "success";
            } else {
                return "failure";
            }

        }

    throw runtime_error(
        "smartmining disable\n"
        + HelpRequiringPassphrase());
#else // ENABLE_WALLET
    throw runtime_error("No wallet support!");
#endif // ENABLE_WALLET
    }

    if(strCommand == "keys")
    {
#ifdef ENABLE_WALLET

        TRY_LOCK(cs_miningkeys,keysLocked);

        if(!keysLocked) throw JSONRPCError(RPC_DATABASE_ERROR, "Mining keys locked..Try it again!");

        auto *pKeyMap = &mapMiningKeysMainnet;

        if( TestNet() ) pKeyMap = &mapMiningKeysTestnet;

        int64_t nKeyStates = sporkManager.GetSporkValue(SPORK_17_MINING_SIGNATURE_PUBKEYS_ENABLED);

        if (params.size() == 3){

            int64_t nKeyId = params[1].get_int64();
            bool fNewState = params[2].get_bool();

            if( !pKeyMap->count(nKeyId) ) throw JSONRPCError(RPC_INVALID_PARAMETER, "Mining key index out of range!");

            int64_t nKeyMask = 1 << nKeyId;

            bool isEnabled = ( nKeyStates & nKeyMask ) == nKeyMask;

            if( isEnabled && fNewState ) throw JSONRPCError(RPC_INVALID_PARAMETER, "Mining key is already enabled!");
            if( !isEnabled && !fNewState ) throw JSONRPCError(RPC_INVALID_PARAMETER, "Mining key is already disabled!");

            nKeyStates ^= nKeyMask;

            //broadcast new spork
            if(sporkManager.UpdateSpork(SPORK_17_MINING_SIGNATURE_PUBKEYS_ENABLED, nKeyStates, *g_connman)){
                return "success";
            } else {
                return "failure";
            }

        }

        throw runtime_error(
            "smartmining keys [<keyId>] [<newState>]\n"
            "<keyId> is the number of the key to change.\n"
            "<newState> true/false to enable/disable the key.\n"
            + HelpRequiringPassphrase());
#else // ENABLE_WALLET
        throw runtime_error("No wallet support!");
#endif // ENABLE_WALLET

    }

    if (strCommand == "blocks")
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Not available!");
    }

    if (strCommand == "warnings")
    {
        UniValue warnings(UniValue::VARR);
        return warnings;
    }

    return NullUniValue;
}
