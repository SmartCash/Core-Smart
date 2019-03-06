// Copyright (c) 2018-2019 The SmartCash developers
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


int64_t GetKeyForBlock(const CBlockIndex * pIndex){

    CBlock block;
    if(pIndex && ReadBlockFromDisk(block, pIndex, Params().GetConsensus())){

        if( block.vtx.size() >= 1 ){

            if( block.vtx[0].vout.size() >= 2){

                // Second output of the coinbase needs to be the signature.
                const CScript &sigScript = block.vtx[0].vout[1].scriptPubKey;

                // Check if it is an OP_RETURN and if the startvalue is OP_DATA_MINING_FLAG
                if( sigScript.size() > nMiningSignatureMinScriptLength &&
                    sigScript[0] == OP_RETURN && sigScript[2] == OP_RETURN_MINING_FLAG ){
                    return sigScript[3];
                }

            }else{
                return -3;
            }

        }else{
            return -2;
        }

    }

    return -1;
}

std::pair<int64_t,int64_t> GetBlockRange(const UniValue& params)
{
    int64_t nStart, nStop;

    CBlockIndex *pIndex = chainActive.Tip();

    if( params.size() > 2 ){
        nStart = params[1].get_int64();
        nStop = params[2].get_int64();
    }else{
        nStart = pIndex->nHeight - params[1].get_int64();
        nStop = pIndex->nHeight;
    }

    return make_pair(nStart, nStop);
}

UniValue smartmining(const UniValue& params, bool fHelp)
{

    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();
    }

    if (fHelp  ||
        (
         strCommand != "status" && strCommand != "keys" && strCommand != "blocks" && strCommand != "count" && strCommand != "block" && strCommand != "blocktime" && strCommand != "disable" && strCommand != "enable" && strCommand != "warnings"))
            throw std::runtime_error(
                "smartmining \"command\"...\n"
                "Set of commands to execute smartmining related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
                "  status                - Print the current status of the enforcement and the keys.\n"
                "  block :height         - Print the key used at :height.\n"
                "  blocks :blocks        - Print a list of the keys used in the last :blocks blocks.\n"
                "  count :blocks         - Print a summary of the keys used in the last :blocks blocks.\n"
                "  blocktime :blocks     - Print the avg blocktime of the last :blocks blocks.\n"
                //"  warnings :count       - Check the last :count blocks for strange abnormalities.\n"
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

    if (strCommand == "block")
    {

        UniValue obj = UniValue(UniValue::VOBJ);

        if (params.size() == 2 && params[1].get_int64() > 0){

            LOCK(cs_main);

            int64_t nHeight = params[1].get_int64();
            CBlockIndex * pIndex = chainActive[nHeight];

            if( !pIndex ) throw JSONRPCError(RPC_INVALID_PARAMETER,"Index out of range");

            obj.pushKV(std::to_string(pIndex->nHeight),GetKeyForBlock(pIndex));

            return obj;
        }

        throw runtime_error(
            "smartmining block <blockHeight>\n"
            "<blockHeight> is the height of the block to check.\n");

    }

    if (strCommand == "blocks")
    {

        UniValue obj = UniValue(UniValue::VOBJ);

        if (params.size() >= 2 && params.size() <= 3){

            LOCK(cs_main);

            auto range = GetBlockRange(params);

            CBlockIndex *pIndex = NULL;
            CBlockIndex *pLastIndex = NULL;

            pIndex = chainActive[range.first];
            pLastIndex = chainActive[range.first - 1 ];

            if( pLastIndex && range.first < range.second ){

                while( pIndex && pIndex->nHeight != range.second ){

                    UniValue block(UniValue::VOBJ);

                    int64_t nKey = GetKeyForBlock(pIndex);
                    block.pushKV("key", nKey);
                    block.pushKV("blocktime", pIndex->GetBlockTime() - pLastIndex->GetBlockTime());
                    obj.pushKV(std::to_string(pIndex->nHeight),block);

                    pLastIndex = pIndex;
                    pIndex = chainActive.Next(pIndex);
                }

            }

            return obj;
        }

        throw runtime_error(
            "smartmining blocks <blockCount>\n"
            "<blockCount> is the number of past blocks to check.\n");

    }

    if (strCommand == "blocktime")
    {

        UniValue obj = UniValue(UniValue::VOBJ);

        if (params.size() >= 2 && params.size() <= 3){

            LOCK(cs_main);

            auto range = GetBlockRange(params);

            CBlockIndex *pIndex = NULL;
            CBlockIndex *pLastIndex = NULL;
            int64_t nCount = range.second - range.first;
            int64_t nBlockTime = 0;
            int64_t nOddCount = 0;
            int64_t nOddSum = 0;
            int64_t nEvenCount = 0;
            int64_t nEvenSum = 0;
            int64_t nMinBlockTime = INT_MAX;
            int64_t nMaxBlockTime = INT_MIN;

            pIndex = chainActive[range.first];
            pLastIndex = chainActive[range.first - 1 ];

            if( pLastIndex && range.first < range.second){

                while( pIndex && pIndex->nHeight != range.second ){

                    nBlockTime = pIndex->GetBlockTime() - pLastIndex->GetBlockTime();

                    if( pIndex->nHeight % 2 ){
                        ++nOddCount;
                        nOddSum += nBlockTime;
                    }else{
                        ++nEvenCount;
                        nEvenSum += nBlockTime;
                    }

                    if( nBlockTime < nMinBlockTime ) nMinBlockTime = nBlockTime;
                    if( nBlockTime > nMaxBlockTime ) nMaxBlockTime = nBlockTime;

                    pLastIndex = pIndex;
                    pIndex = chainActive.Next(pIndex);
                }

                obj.pushKV("shortest", nMinBlockTime);
                obj.pushKV("longest", nMaxBlockTime);
                if( nCount > 1 ){
                    obj.pushKV("odd", nOddSum / nOddCount );
                    obj.pushKV("even", nEvenSum / nEvenCount );
                }

                obj.pushKV("average", (nOddSum + nEvenSum) / (nOddCount + nEvenCount));

            }

            return obj;
        }

        throw runtime_error(
            "smartmining blocktime <blockCount>\n"
            "<blockCount> is the number of past blocks to check.\n");
    }

    if (strCommand == "count")
    {

        UniValue obj = UniValue(UniValue::VOBJ);

        if (params.size() == 2 && params[1].get_int64() > 0){

            LOCK(cs_main);

            std::map<int64_t,int64_t> mapUsage;

            CBlockIndex * pIndex = chainActive.Tip();

            int64_t nCount = params[1].get_int64();
            int64_t nStartHeight = pIndex->nHeight - nCount + 1;

            pIndex = chainActive[nStartHeight];

            while( pIndex ){
                mapUsage[GetKeyForBlock(pIndex)]++;
                pIndex = chainActive.Next(pIndex);
            }

            auto it = mapUsage.begin();

            while( it != mapUsage.end() ){
                obj.pushKV(std::to_string(it->first), it->second);
                it++;
            }


            return obj;
        }

        throw runtime_error(
            "smartmining blocks <blockCount>\n"
            "<blockCount> is the number of past blocks to check.\n");
    }

    if (strCommand == "warnings")
    {
        UniValue warnings(UniValue::VARR);
        return warnings;
    }

    return NullUniValue;
}
