// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "init.h"
#include "validation.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "smartnode/activesmartnode.h"
#include "smartnode/smartnodeconfig.h"
#include "smartnode/smartnodeman.h"
#include "smartnode/smartnodepayments.h"
#include "smartnode/smartnodesync.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet/wallet.h"

#include <fstream>
#include <iomanip>
#include <univalue.h>

void EnsureWalletIsUnlocked();

// UniValue privatesend(const UniValue& params, bool fHelp)
// {
//     if (fHelp || params.size() != 1)
//         throw std::runtime_error(
//             "privatesend \"command\"\n"
//             "\nArguments:\n"
//             "1. \"command\"        (string or set of strings, required) The command to execute\n"
//             "\nAvailable commands:\n"
//             "  start       - Start mixing\n"
//             "  stop        - Stop mixing\n"
//             "  reset       - Reset mixing\n"
//             );

//     if(params[0].get_str() == "start") {
//         {
//             LOCK(pwalletMain->cs_wallet);
//             EnsureWalletIsUnlocked();
//         }

//         if(fSmartNode)
//             return "Mixing is not supported from smartnodes";

//         privateSendClient.fEnablePrivateSend = true;
//         bool result = privateSendClient.DoAutomaticDenominating(*g_connman);
//         return "Mixing " + (result ? "started successfully" : ("start failed: " + privateSendClient.GetStatus() + ", will retry"));
//     }

//     if(params[0].get_str() == "stop") {
//         privateSendClient.fEnablePrivateSend = false;
//         return "Mixing was stopped";
//     }

//     if(params[0].get_str() == "reset") {
//         privateSendClient.ResetPool();
//         return "Mixing was reset";
//     }

//     return "Unknown command, please see \"help privatesend\"";
// }

// UniValue getpoolinfo(const UniValue& params, bool fHelp)
// {
//     if (fHelp || params.size() != 0)
//         throw std::runtime_error(
//             "getpoolinfo\n"
//             "Returns an object containing mixing pool related information.\n");

// #ifdef ENABLE_WALLET
//     CPrivateSendBase* pprivateSendBase = fSmartNode ? (CPrivateSendBase*)&privateSendServer : (CPrivateSendBase*)&privateSendClient;

//     UniValue obj(UniValue::VOBJ);
//     obj.push_back(Pair("state",             pprivateSendBase->GetStateString()));
//     obj.push_back(Pair("mixing_mode",       (!fSmartNode && privateSendClient.fPrivateSendMultiSession) ? "multi-session" : "normal"));
//     obj.push_back(Pair("queue",             pprivateSendBase->GetQueueSize()));
//     obj.push_back(Pair("entries",           pprivateSendBase->GetEntriesCount()));
//     obj.push_back(Pair("status",            privateSendClient.GetStatus()));

//     smartnode_info_t mnInfo;
//     if (privateSendClient.GetMixingSmartnodeInfo(mnInfo)) {
//         obj.push_back(Pair("outpoint",      mnInfo.vin.prevout.ToStringShort()));
//         obj.push_back(Pair("addr",          mnInfo.addr.ToString()));
//     }

//     if (pwalletMain) {
//         obj.push_back(Pair("keys_left",     pwalletMain->nKeysLeftSinceAutoBackup));
//         obj.push_back(Pair("warnings",      pwalletMain->nKeysLeftSinceAutoBackup < PRIVATESEND_KEYS_THRESHOLD_WARNING
//                                                 ? "WARNING: keypool is almost depleted!" : ""));
//     }
// #else // ENABLE_WALLET
//     UniValue obj(UniValue::VOBJ);
//     obj.push_back(Pair("state",             privateSendServer.GetStateString()));
//     obj.push_back(Pair("queue",             privateSendServer.GetQueueSize()));
//     obj.push_back(Pair("entries",           privateSendServer.GetEntriesCount()));
// #endif // ENABLE_WALLET

//     return obj;
// }


UniValue smartnode(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-many")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "DEPRECATED, please use start-all instead");
#endif // ENABLE_WALLET

    if (fHelp  ||
        (
#ifdef ENABLE_WALLET
            strCommand != "start-alias" && strCommand != "start-all" && strCommand != "start-missing" &&
         strCommand != "start-disabled" && strCommand != "outputs" &&
#endif // ENABLE_WALLET
         strCommand != "list" && strCommand != "list-conf" && strCommand != "count" &&
         strCommand != "debug" && strCommand != "current" && strCommand != "winner" && strCommand != "winners" && strCommand != "genkey" &&
         strCommand != "connect" && strCommand != "status" && strCommand != "protocol"))
            throw std::runtime_error(
                "smartnode \"command\"...\n"
                "Set of commands to execute smartnode related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
                "  count        - Print number of all known smartnodes (optional: 'ps', 'enabled', 'all', 'qualify', 'states')\n"
                "  current      - Print info on current smartnode winner to be paid the next block (calculated locally)\n"
                "  genkey       - Generate new smartnodeprivkey\n"
#ifdef ENABLE_WALLET
                "  outputs      - Print smartnode compatible outputs\n"
                "  start-alias  - Start single remote smartnode by assigned alias configured in smartnode.conf\n"
                "  start-<mode> - Start remote smartnodes configured in smartnode.conf (<mode>: 'all', 'missing', 'disabled')\n"
#endif // ENABLE_WALLET
                "  status       - Print smartnode status information\n"
                "  list         - Print list of all known smartnodes (see smartnodelist for more info)\n"
                "  list-conf    - Print smartnode.conf in JSON format\n"
                "  winner       - Print info on next smartnode winner to vote for\n"
                "  winners      - Print list of smartnode winners\n"
                );

    if (strCommand == "list")
    {
        UniValue newParams(UniValue::VARR);
        // forward params but skip "list"
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return smartnodelist(newParams, fHelp);
    }

    if(strCommand == "connect")
    {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Smartnode address required");

        std::string strAddress = params[1].get_str();

        CService addr;
        if (!Lookup(strAddress.c_str(), addr, 0, false))
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect smartnode address %s", strAddress));

        // TODO: Pass CConnman instance somehow and don't use global variable.
        CNode *pnode = g_connman->ConnectNode(CAddress(addr, NODE_NETWORK), NULL);
        if(!pnode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to smartnode %s", strAddress));

        return "successfully connected";
    }

    if (strCommand == "count")
    {
        if (params.size() > 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");

        if (params.size() == 1)
            return mnodeman.size();

        std::string strMode = params[1].get_str();

        if (strMode == "enabled")
            return mnodeman.CountEnabled();

        int nCount;
        CSmartNodeWinners mnInfos;
        mnodeman.GetNextSmartnodesInQueueForPayment(true, nCount, mnInfos);

        if (strMode == "qualify")
            return nCount;

        if (strMode == "all")
            return strprintf("Total: %d ( Enabled: %d / Qualify: %d)",
                mnodeman.size(), mnodeman.CountEnabled(), nCount);

        if (strMode == "states"){

            UniValue obj = UniValue(UniValue::VOBJ);

            std::map<std::string, int64_t> mapStates;

            mnodeman.CountStates(mapStates);

            for (auto& state : mapStates) {
                obj.pushKV(state.first, state.second);
            }

            return obj;
        }
    }

    if (strCommand == "current" || strCommand == "winner")
    {
        int nCount;
        int nHeight;
        CSmartNodeWinners mnInfos;
        CBlockIndex* pindex = NULL;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
        nHeight = pindex->nHeight + (strCommand == "current" ? 1 : 10);
        mnodeman.UpdateLastPaid(pindex);

        if(!mnodeman.GetNextSmartnodesInQueueForPayment(nHeight, true, nCount, mnInfos))
            return "unknown";

        UniValue obj(UniValue::VOBJ);
        UniValue nodes(UniValue::VARR);

        obj.push_back(Pair("height", nHeight));

        for(auto& mnInfo : mnInfos )
        {
            UniValue node(UniValue::VOBJ);

            node.push_back(Pair("IP:port",       mnInfo.addr.ToString()));
            node.push_back(Pair("protocol",      (int64_t)mnInfo.nProtocolVersion));
            node.push_back(Pair("outpoint",      mnInfo.vin.prevout.ToStringShort()));
            node.push_back(Pair("payee",         CBitcoinAddress(mnInfo.pubKeyCollateralAddress.GetID()).ToString()));
            node.push_back(Pair("lastseen",      mnInfo.nTimeLastPing));
            node.push_back(Pair("activeseconds", mnInfo.nTimeLastPing - mnInfo.sigTime));

            nodes.push_back(node);
        }

        obj.pushKV("nodes", nodes);

        return obj;
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-alias")
    {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        std::string strAlias = params[1].get_str();

        bool fFound = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", strAlias));

        BOOST_FOREACH(CSmartnodeConfigEntry mne, smartnodeConfig.getEntries()) {
            if(mne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CSmartnodeBroadcast mnb;

                bool fResult = CSmartnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

                int nDoS;
                if (fResult && !mnodeman.CheckMnbAndUpdateSmartnodeList(NULL, mnb, nDoS, *g_connman)) {
                    strError = "Please wait 15 confirmations or check your configuration";
                    fResult = false;
                }

                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

                if(!fResult) {
                    statusObj.push_back(Pair("errorMessage", strError));
                }

                mnodeman.NotifySmartnodeUpdates(*g_connman);
                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;

    }

    if (strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled")
    {
        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        if((strCommand == "start-missing" || strCommand == "start-disabled") && !smartnodeSync.IsSmartnodeListSynced()) {
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "You can't use this command until smartnode list is synced");
        }

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);

        BOOST_FOREACH(CSmartnodeConfigEntry mne, smartnodeConfig.getEntries()) {
            std::string strError;

            COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CSmartnode mn;
            bool fFound = mnodeman.Get(outpoint, mn);
            CSmartnodeBroadcast mnb;

            if(strCommand == "start-missing" && fFound) continue;
            if(strCommand == "start-disabled" && fFound && mn.IsEnabled()) continue;

            bool fResult = CSmartnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            int nDoS;
            if (fResult && !mnodeman.CheckMnbAndUpdateSmartnodeList(NULL, mnb, nDoS, *g_connman)) {
                strError = "Please wait 15 confirmations or check your configuration";
                fResult = false;
            }

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

            if (fResult) {
                nSuccessful++;
            } else {
                nFailed++;
                statusObj.push_back(Pair("errorMessage", strError));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }
        mnodeman.NotifySmartnodeUpdates(*g_connman);

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d smartnodes, failed to start %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "genkey")
    {
        CKey secret;
        secret.MakeNewKey(false);

        return CBitcoinSecret(secret).ToString();
    }

    if (strCommand == "list-conf")
    {
        UniValue resultObj(UniValue::VOBJ);

        BOOST_FOREACH(CSmartnodeConfigEntry mne, smartnodeConfig.getEntries()) {
            COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CSmartnode mn;
            bool fFound = mnodeman.Get(outpoint, mn);

            std::string strStatus = fFound ? mn.GetStatus() : "MISSING";

            UniValue mnObj(UniValue::VOBJ);
            mnObj.push_back(Pair("alias", mne.getAlias()));
            mnObj.push_back(Pair("address", mne.getIp()));
            mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
            mnObj.push_back(Pair("txHash", mne.getTxHash()));
            mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
            mnObj.push_back(Pair("status", strStatus));
            resultObj.push_back(Pair("smartnode", mnObj));
        }

        return resultObj;
    }

#ifdef ENABLE_WALLET
    if (strCommand == "outputs") {
        // Find possible candidates
        std::vector<COutput> vPossibleCoins;
        pwalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, ONLY_10000);

        UniValue obj(UniValue::VOBJ);
        UniValue used(UniValue::VARR);
        UniValue unused(UniValue::VARR);

        BOOST_FOREACH(COutput& out, vPossibleCoins) {

            UniValue entry(UniValue::VOBJ);

            BOOST_FOREACH(CSmartnodeConfigEntry mne, smartnodeConfig.getEntries()) {

                COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));

                if( out.tx->GetHash() == outpoint.hash && out.i == (int)outpoint.n){
                    entry.pushKV("alias", mne.getAlias());
                    entry.pushKV("collateral_output_txid", outpoint.hash.ToString());
                    entry.pushKV("collateral_output_index", (int)outpoint.n);
                    break;
                }
            }

            if( entry.size() ){
                used.push_back(entry);
            }else{
                entry.pushKV("collateral_output_txid", out.tx->GetHash().ToString());
                entry.pushKV("collateral_output_index", out.i);
                unused.push_back(entry);
            }

        }

        obj.pushKV("used_collaterals", used);
        obj.pushKV("new_collaterals", unused);

        return obj;
    }

#endif // ENABLE_WALLET

    if (strCommand == "status")
    {
        if (!fSmartNode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a smartnode");

        UniValue mnObj(UniValue::VOBJ);

        mnObj.push_back(Pair("outpoint", activeSmartnode.outpoint.ToStringShort()));
        mnObj.push_back(Pair("service", activeSmartnode.service.ToString()));

        CSmartnode mn;
        if(mnodeman.Get(activeSmartnode.outpoint, mn)) {
            mnObj.push_back(Pair("payee", CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString()));
        }

        mnObj.push_back(Pair("status", activeSmartnode.GetStatus()));
        return mnObj;
    }

    if (strCommand == "winners")
    {
        int nHeight;
        {
            LOCK(cs_main);
            CBlockIndex* pindex = chainActive.Tip();
            if(!pindex) return NullUniValue;

            nHeight = pindex->nHeight;
        }

        int nLast = 10;

        if (params.size() >= 2) {
            nLast = atoi(params[1].get_str());
        }

        if (params.size() > 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartnode winners ( \"count\" )'");

        UniValue obj(UniValue::VOBJ);

        for(int i = nHeight - nLast; i < nHeight + MNPAYMENTS_FUTURE_VOTES + SmartNodePayments::PayoutInterval(nHeight) + 1; i++) {
            UniValue payment = SmartNodePayments::GetPaymentBlockObject(i);
            obj.push_back(Pair(strprintf("%d", i), payment));
        }

        return obj;
    }

    if(strCommand == "protocol")
    {
#ifdef ENABLE_WALLET

        if (params.size() >= 3){

            int64_t nProtocolOld = atoi(params[1].get_str());
            int64_t nProtocolNew = atoi(params[2].get_str());
            int64_t nEnableTime = params.size() == 4 ? atoi(params[3].get_str()) : 0x7FFFFFFFFFFF;

            if(  nProtocolOld < PROTOCOL_BASE_VERSION || nProtocolOld > PROTOCOL_MAX_VERSION ){
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Protocol old out of range!");
            }

            if(  nProtocolNew < PROTOCOL_BASE_VERSION || nProtocolNew > PROTOCOL_MAX_VERSION ){
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Protocol new out of range!");
            }

            // Only allow to set the time from now to 90 days in the future or to 0 to instantly enable the new protocol
            if( nEnableTime != 0 && nEnableTime != 0x7FFFFFFFFFFF && ( nEnableTime < GetAdjustedTime() || nEnableTime > GetAdjustedTime() + (90 * 24 * 60 * 60) ) ){
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Enable time points to the past or >90 days in the futures!");
            }

            nProtocolOld -= PROTOCOL_BASE_VERSION;
            nProtocolNew -= PROTOCOL_BASE_VERSION;

            int64_t nProtocolSpork = nEnableTime << 16 | nProtocolNew << 8 | nProtocolOld;

            LogPrintf("Set protocol old to %d\n", nProtocolOld);
            LogPrintf("Set protocol new to %d\n", nProtocolNew);
            LogPrintf("Set protocol activation time to %d\n", nEnableTime);
            LogPrintf("Result value %08X\n", nProtocolSpork);

            //broadcast new spork
            if(sporkManager.UpdateSpork(SPORK_21_SMARTNODE_PROTOCOL_REQUIREMENT, nProtocolSpork, *g_connman)){
                return "success";
            } else {
                return "failure";
            }

        }else{
            UniValue protocolResult(UniValue::VOBJ);

            int64_t nProtocolSpork = sporkManager.GetSporkValue(SPORK_21_SMARTNODE_PROTOCOL_REQUIREMENT);

            int nProtocolOld = PROTOCOL_BASE_VERSION + (nProtocolSpork & 0xFF);
            int nProtocolNew = PROTOCOL_BASE_VERSION + ((nProtocolSpork >> 8) & 0xFF);
            int64_t nProtocolActiveTime = ( nProtocolSpork & 0xFFFFFFFFFFFFFF)  >> 16;

            protocolResult.pushKV("oldProtocol", nProtocolOld);
            protocolResult.pushKV("newProtocol", nProtocolNew);
            protocolResult.pushKV("enableTime", nProtocolActiveTime);
            protocolResult.pushKV("activeProtocol", mnpayments.GetMinSmartnodePaymentsProto());

            return protocolResult;
        }

#else // ENABLE_WALLET
        throw runtime_error("No wallet support!");
#endif // ENABLE_WALLET

    }

    return NullUniValue;
}

UniValue smartnodelist(const UniValue& params, bool fHelp)
{
    std::string strMode = "status";
    std::string strFilter = "";

    if (params.size() >= 1) strMode = params[0].get_str();
    if (params.size() == 2) strFilter = params[1].get_str();

    if (fHelp || (
                strMode != "activeseconds" && strMode != "addr" && strMode != "full" && strMode != "info" &&
                strMode != "lastseen" && strMode != "lastpaidtime" && strMode != "lastpaidblock" &&
                strMode != "protocol" && strMode != "payee" && strMode != "pubkey" &&
                strMode != "rank" && strMode != "status"))
    {
        throw std::runtime_error(
                "smartnodelist ( \"mode\" \"filter\" )\n"
                "Get a list of smartnodes in different modes\n"
                "\nArguments:\n"
                "1. \"mode\"      (string, optional/required to use filter, defaults = status) The mode to run list in\n"
                "2. \"filter\"    (string, optional) Filter results. Partial match by outpoint by default in all modes,\n"
                "                                    additional matches in some modes are also available\n"
                "\nAvailable modes:\n"
                "  activeseconds  - Print number of seconds smartnode recognized by the network as enabled\n"
                "                   (since latest issued \"smartnode start/start-many/start-alias\")\n"
                "  addr           - Print ip address associated with a smartnode (can be additionally filtered, partial match)\n"
                "  full           - Print info in format 'status protocol payee lastseen activeseconds lastpaidtime lastpaidblock IP'\n"
                "                   (can be additionally filtered, partial match)\n"
                "  info           - Print info in format 'status protocol payee lastseen activeseconds sentinelversion sentinelstate IP'\n"
                "                   (can be additionally filtered, partial match)\n"
                "  lastpaidblock  - Print the last block height a node was paid on the network\n"
                "  lastpaidtime   - Print the last time a node was paid on the network\n"
                "  lastseen       - Print timestamp of when a smartnode was last seen on the network\n"
                "  payee          - Print SmartCash address associated with a smartnode (can be additionally filtered,\n"
                "                   partial match)\n"
                "  protocol       - Print protocol of a smartnode (can be additionally filtered, exact match)\n"
                "  pubkey         - Print the smartnode (not collateral) public key\n"
                "  rank           - Print rank of a smartnode based on current block\n"
                "  status         - Print smartnode status: PRE_ENABLED / ENABLED / EXPIRED / NEW_START_REQUIRED /\n"
                "                   UPDATE_REQUIRED / POSE_BAN / OUTPOINT_SPENT (can be additionally filtered, partial match)\n"
                );
    }

    if (strMode == "full" || strMode == "lastpaidtime" || strMode == "lastpaidblock") {
        CBlockIndex* pindex = NULL;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
        mnodeman.UpdateLastPaid(pindex);
    }

    UniValue obj(UniValue::VOBJ);
    if (strMode == "rank") {
        CSmartnodeMan::rank_pair_vec_t vSmartnodeRanks;
        mnodeman.GetSmartnodeRanks(vSmartnodeRanks);
        BOOST_FOREACH(PAIRTYPE(int, CSmartnode)& s, vSmartnodeRanks) {
            std::string strOutpoint = s.second.vin.prevout.ToStringShort();
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, s.first));
        }
    } else {
        std::map<COutPoint, CSmartnode> mapSmartnodes = mnodeman.GetFullSmartnodeMap();
        for (auto& mnpair : mapSmartnodes) {
            CSmartnode mn = mnpair.second;
            std::string strOutpoint = mnpair.first.ToStringShort();
            if (strMode == "activeseconds") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)(mn.lastPing.sigTime - mn.sigTime)));
            } else if (strMode == "addr") {
                std::string strAddress = mn.addr.ToString();
                if (strFilter !="" && strAddress.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strAddress));
            } else if (strMode == "full") {
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
                if (strFilter !="" && strFull.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strFull));
            } else if (strMode == "info") {
                std::ostringstream streamInfo;
                streamInfo << std::setw(18) <<
                               mn.GetStatus() << " " <<
                               mn.nProtocolVersion << " " <<
                               CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString() << " " <<
                               (int64_t)mn.lastPing.sigTime << " " << std::setw(8) <<
                               (int64_t)(mn.lastPing.sigTime - mn.sigTime) << " " <<
                               SafeIntVersionToString(mn.lastPing.nSentinelVersion) << " "  <<
                               (mn.lastPing.fSentinelIsCurrent ? "current" : "expired") << " " <<
                               mn.addr.ToString();
                std::string strInfo = streamInfo.str();
                if (strFilter !="" && strInfo.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strInfo));
            } else if (strMode == "lastpaidblock") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, mn.GetLastPaidBlock()));
            } else if (strMode == "lastpaidtime") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, mn.GetLastPaidTime()));
            } else if (strMode == "lastseen") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)mn.lastPing.sigTime));
            } else if (strMode == "payee") {
                CBitcoinAddress address(mn.pubKeyCollateralAddress.GetID());
                std::string strPayee = address.ToString();
                if (strFilter !="" && strPayee.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strPayee));
            } else if (strMode == "protocol") {
                if (strFilter !="" && strFilter != strprintf("%d", mn.nProtocolVersion) &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)mn.nProtocolVersion));
            } else if (strMode == "pubkey") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, HexStr(mn.pubKeySmartnode)));
            } else if (strMode == "status") {
                std::string strStatus = mn.GetStatus();
                if (strFilter !="" && strStatus.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strStatus));
            }
        }
    }
    return obj;
}

bool DecodeHexVecMnb(std::vector<CSmartnodeBroadcast>& vecMnb, std::string strHexMnb) {

    if (!IsHex(strHexMnb))
        return false;

    std::vector<unsigned char> mnbData(ParseHex(strHexMnb));
    CDataStream ssData(mnbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> vecMnb;
    }
    catch (const std::exception&) {
        return false;
    }

    return true;
}

UniValue smartnodebroadcast(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp  ||
        (
#ifdef ENABLE_WALLET
            strCommand != "create-alias" && strCommand != "create-all" &&
#endif // ENABLE_WALLET
            strCommand != "decode" && strCommand != "relay"))
        throw std::runtime_error(
                "smartnodebroadcast \"command\"...\n"
                "Set of commands to create and relay smartnode broadcast messages\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
#ifdef ENABLE_WALLET
                "  create-alias  - Create single remote smartnode broadcast message by assigned alias configured in smartnode.conf\n"
                "  create-all    - Create remote smartnode broadcast messages for all smartnodes configured in smartnode.conf\n"
#endif // ENABLE_WALLET
                "  decode        - Decode smartnode broadcast message\n"
                "  relay         - Relay smartnode broadcast message to the network\n"
                );

#ifdef ENABLE_WALLET
    if (strCommand == "create-alias")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        bool fFound = false;
        std::string strAlias = params[1].get_str();

        UniValue statusObj(UniValue::VOBJ);
        std::vector<CSmartnodeBroadcast> vecMnb;

        statusObj.push_back(Pair("alias", strAlias));

        BOOST_FOREACH(CSmartnodeConfigEntry mne, smartnodeConfig.getEntries()) {
            if(mne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CSmartnodeBroadcast mnb;

                bool fResult = CSmartnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb, true);

                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
                if(fResult) {
                    vecMnb.push_back(mnb);
                    CDataStream ssVecMnb(SER_NETWORK, PROTOCOL_VERSION);
                    ssVecMnb << vecMnb;
                    statusObj.push_back(Pair("hex", HexStr(ssVecMnb.begin(), ssVecMnb.end())));
                } else {
                    statusObj.push_back(Pair("errorMessage", strError));
                }
                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair("result", "not found"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;

    }

    if (strCommand == "create-all")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        std::vector<CSmartnodeConfigEntry> mnEntries;
        mnEntries = smartnodeConfig.getEntries();

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);
        std::vector<CSmartnodeBroadcast> vecMnb;

        BOOST_FOREACH(CSmartnodeConfigEntry mne, smartnodeConfig.getEntries()) {
            std::string strError;
            CSmartnodeBroadcast mnb;

            bool fResult = CSmartnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb, true);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

            if(fResult) {
                nSuccessful++;
                vecMnb.push_back(mnb);
            } else {
                nFailed++;
                statusObj.push_back(Pair("errorMessage", strError));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }

        CDataStream ssVecMnb(SER_NETWORK, PROTOCOL_VERSION);
        ssVecMnb << vecMnb;
        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully created broadcast messages for %d smartnodes, failed to create %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));
        returnObj.push_back(Pair("hex", HexStr(ssVecMnb.begin(), ssVecMnb.end())));

        return returnObj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "decode")
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartnodebroadcast decode \"hexstring\"'");

        std::vector<CSmartnodeBroadcast> vecMnb;

        if (!DecodeHexVecMnb(vecMnb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Smartnode broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        int nDos = 0;
        UniValue returnObj(UniValue::VOBJ);

        BOOST_FOREACH(CSmartnodeBroadcast& mnb, vecMnb) {
            UniValue resultObj(UniValue::VOBJ);

            if(mnb.CheckSignature(nDos)) {
                nSuccessful++;
                resultObj.push_back(Pair("outpoint", mnb.vin.prevout.ToStringShort()));
                resultObj.push_back(Pair("addr", mnb.addr.ToString()));
                resultObj.push_back(Pair("pubKeyCollateralAddress", CBitcoinAddress(mnb.pubKeyCollateralAddress.GetID()).ToString()));
                resultObj.push_back(Pair("pubKeySmartnode", CBitcoinAddress(mnb.pubKeySmartnode.GetID()).ToString()));
                resultObj.push_back(Pair("vchSig", EncodeBase64(&mnb.vchSig[0], mnb.vchSig.size())));
                resultObj.push_back(Pair("sigTime", mnb.sigTime));
                resultObj.push_back(Pair("protocolVersion", mnb.nProtocolVersion));
                resultObj.push_back(Pair("nLastDsq", mnb.nLastDsq));

                UniValue lastPingObj(UniValue::VOBJ);
                lastPingObj.push_back(Pair("outpoint", mnb.lastPing.outpoint.ToStringShort()));
                lastPingObj.push_back(Pair("blockHash", mnb.lastPing.blockHash.ToString()));
                lastPingObj.push_back(Pair("sigTime", mnb.lastPing.sigTime));
                lastPingObj.push_back(Pair("vchSig", EncodeBase64(&mnb.lastPing.vchSig[0], mnb.lastPing.vchSig.size())));

                resultObj.push_back(Pair("lastPing", lastPingObj));
            } else {
                nFailed++;
                resultObj.push_back(Pair("errorMessage", "Smartnode broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(mnb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf("Successfully decoded broadcast messages for %d smartnodes, failed to decode %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));

        return returnObj;
    }

    if (strCommand == "relay")
    {
        if (params.size() < 2 || params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER,   "smartnodebroadcast relay \"hexstring\" ( fast )\n"
                                                        "\nArguments:\n"
                                                        "1. \"hex\"      (string, required) Broadcast messages hex string\n");

        std::vector<CSmartnodeBroadcast> vecMnb;

        if (!DecodeHexVecMnb(vecMnb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Smartnode broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        UniValue returnObj(UniValue::VOBJ);

        // verify all signatures first, bailout if any of them broken
        BOOST_FOREACH(CSmartnodeBroadcast& mnb, vecMnb) {
            UniValue resultObj(UniValue::VOBJ);

            resultObj.push_back(Pair("outpoint", mnb.vin.prevout.ToStringShort()));
            resultObj.push_back(Pair("addr", mnb.addr.ToString()));

            int nDos = 0;
            bool fResult;
            if (mnb.CheckSignature(nDos)) {
                fResult = mnodeman.CheckMnbAndUpdateSmartnodeList(NULL, mnb, nDos, *g_connman);
                mnodeman.NotifySmartnodeUpdates(*g_connman);
            } else fResult = false;

            if(fResult) {
                nSuccessful++;
                resultObj.push_back(Pair(mnb.GetHash().ToString(), "successful"));
            } else {
                nFailed++;
                resultObj.push_back(Pair("errorMessage", "SmartNode broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(mnb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf("Successfully relayed broadcast messages for %d smartnodes, failed to relay %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));

        return returnObj;
    }

    return NullUniValue;
}

UniValue sentinelping(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1) {
        throw std::runtime_error(
            "sentinelping version\n"
            "\nSentinel ping.\n"
            "\nArguments:\n"
            "1. version           (string, required) Sentinel version in the form \"x.x.x\"\n"
            "\nResult:\n"
            "state                (boolean) Ping result\n"
            "\nExamples:\n"
            + HelpExampleCli("sentinelping", "1.0.2")
            + HelpExampleRpc("sentinelping", "1.0.2")
        );
    }

    activeSmartnode.UpdateSentinelPing(StringVersionToInt(params[0].get_str()));
    return true;
}
