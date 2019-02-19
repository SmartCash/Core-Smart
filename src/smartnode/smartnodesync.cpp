// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activesmartnode.h"
#include "checkpoints.h"
#include "validation.h"
#include "smartnode.h"
#include "smartnodepayments.h"
#include "smartnodesync.h"
#include "smartnodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "ui_interface.h"
#include "util.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"

class CSmartnodeSync;
CSmartnodeSync smartnodeSync;

std::map<int, int> mapSmartnodeListCounts;

CCriticalSection cs_unknownpings;
std::map<COutPoint,int> mapTryUnknownPings;

int GetMeanListCount()
{
    int nTotal = 0;
    auto it = mapSmartnodeListCounts.begin();

    while(it != mapSmartnodeListCounts.end() ){
        nTotal += it->second;
        ++it;
    }

    return mapSmartnodeListCounts.size() ? nTotal / mapSmartnodeListCounts.size() : 0;
}

void CSmartnodeSync::Disconnect(int nHowMany, int nMinProtocol)
{
    int nDisconnected = 0;
    g_connman->ForEachNode(CConnman::FullyConnectedOnly, [&nDisconnected, nHowMany, nMinProtocol](CNode* pnode) {

        if( nDisconnected < nHowMany && pnode->nVersion < nMinProtocol ){
            pnode->fDisconnect = true;
            ++nDisconnected;
        }

    });
}

void CSmartnodeSync::Fail(CConnman& connman)
{
    nTimeLastFailure = GetTime();
    nRequestedSmartnodeAssets = SMARTNODE_SYNC_FAILED;
    // If the sync failed disconnect half of the nodes and try again..
    Disconnect(g_connman->GetNodeCount(CConnman::CONNECTIONS_OUT) / 2, mnpayments.GetMinSmartnodePaymentsProto() );
}

void CSmartnodeSync::Reset()
{
    nRequestedSmartnodeAssets = SMARTNODE_SYNC_INITIAL;
    nRequestedSmartnodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastBumped = GetTime();
    nTimeLastFailure = 0;
}

void CSmartnodeSync::BumpAssetLastTime(std::string strFuncName)
{
    if(IsSynced() || IsFailed()) return;
    nTimeLastBumped = GetTime();
    LogPrint("mnsync", "CSmartnodeSync::BumpAssetLastTime -- %s\n", strFuncName);
}

std::string CSmartnodeSync::GetAssetName()
{
    switch(nRequestedSmartnodeAssets)
    {
        case(SMARTNODE_SYNC_INITIAL):      return "SMARTNODE_SYNC_INITIAL";
        case(SMARTNODE_SYNC_WAITING):      return "SMARTNODE_SYNC_WAITING";
        case(SMARTNODE_SYNC_LIST):         return "SMARTNODE_SYNC_LIST";
        case(SMARTNODE_SYNC_MNW):          return "SMARTNODE_SYNC_MNW";
        case(SMARTNODE_SYNC_FAILED):       return "SMARTNODE_SYNC_FAILED";
        case SMARTNODE_SYNC_FINISHED:      return "SMARTNODE_SYNC_FINISHED";
        default:                            return "UNKNOWN";
    }
}

void CSmartnodeSync::SwitchToNextAsset(CConnman& connman)
{
    switch(nRequestedSmartnodeAssets)
    {
        case(SMARTNODE_SYNC_FAILED):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case(SMARTNODE_SYNC_INITIAL):
            nRequestedSmartnodeAssets = SMARTNODE_SYNC_WAITING;
            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(SMARTNODE_SYNC_WAITING):
            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);

            if(fLiteMode){
                nRequestedSmartnodeAssets = SMARTNODE_SYNC_FINISHED;
                uiInterface.NotifyAdditionalDataSyncProgressChanged(1);
                break;
            }

            nRequestedSmartnodeAssets = SMARTNODE_SYNC_LIST;
            {
                LOCK(cs_unknownpings);
                mapTryUnknownPings.clear();
            }
            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(SMARTNODE_SYNC_LIST):
            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nRequestedSmartnodeAssets = SMARTNODE_SYNC_MNW;
            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(SMARTNODE_SYNC_MNW):
            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nRequestedSmartnodeAssets = SMARTNODE_SYNC_FINISHED;
            uiInterface.NotifyAdditionalDataSyncProgressChanged(1);

            {
                LOCK(cs_unknownpings);
                mapTryUnknownPings.clear();
            }

            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Sync has finished\n");

            //try to activate our smartnode if possible
            activeSmartnode.ManageState(connman);

            // TODO: Find out whether we can just use LOCK instead of:
            // TRY_LOCK(cs_vNodes, lockRecv);
            // if(lockRecv) { ... }

            connman.ForEachNode(CConnman::FullyConnectedOnly, [](CNode* pnode) {
                netfulfilledman.AddFulfilledRequest(pnode->addr, "full-sync");
            });

            break;
    }
    nRequestedSmartnodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    BumpAssetLastTime("CSmartnodeSync::SwitchToNextAsset");
}

std::string CSmartnodeSync::GetSyncStatus()
{
    switch (smartnodeSync.nRequestedSmartnodeAssets) {
        case SMARTNODE_SYNC_INITIAL:       return _("Synchroning blockchain...");
        case SMARTNODE_SYNC_WAITING:       return _("Synchronization pending...");
        case SMARTNODE_SYNC_LIST:          return _("Synchronizing smartnodes...");
        case SMARTNODE_SYNC_MNW:           return _("Synchronizing smartnode payments...");
        case SMARTNODE_SYNC_FAILED:        return _("Synchronization failed");
        case SMARTNODE_SYNC_FINISHED:      return _("Synchronization finished");
        default:                            return "";
    }
}

void CSmartnodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count

        if(pfrom->nVersion < mnpayments.GetMinSmartnodePaymentsProto()){
            LogPrint("mnpayments", "SYNCSTATUSCOUNT - peer=%d using not supported version for smartnode sync %i\n", pfrom->id, pfrom->nVersion);
            connman.PushMessageWithVersion(pfrom, INIT_PROTO_VERSION, NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", mnpayments.GetMinSmartnodePaymentsProto()));
            return;
        }

        //do not care about stats if sync process finished or failed
        if(IsSynced() || IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if( nItemID == SMARTNODE_SYNC_LIST && !mapSmartnodeListCounts.count(pfrom->id))
            mapSmartnodeListCounts.insert(std::make_pair(pfrom->id,nCount));

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->id);
    }
}

void CSmartnodeSync::ProcessTick(CConnman& connman)
{
    static int nTick = 0;
    if(nTick++ % SMARTNODE_SYNC_TICK_SECONDS != 0) return;

    // reset the sync process if the last call to this function was more than 60 minutes ago (client was in sleep mode)
    static int64_t nTimeLastProcess = GetTime();
    if(GetTime() - nTimeLastProcess > 60*60) {
        LogPrintf("CSmartnodeSync::HasSyncFailures -- WARNING: no actions for too long, restarting sync...\n");
        Reset();
        SwitchToNextAsset(connman);
        nTimeLastProcess = GetTime();
        return;
    }
    nTimeLastProcess = GetTime();

    // reset sync status in case of any other sync failure
    if(IsFailed()) {
        if(nTimeLastFailure + (1*60) < GetTime()) { // 1 minute cooldown after failed sync
            LogPrintf("CSmartnodeSync::HasSyncFailures -- WARNING: failed to sync, trying again...\n");
            Reset();
            SwitchToNextAsset(connman);
        }
        return;
    }

    // gradually request the rest of the votes after sync finished
    if(IsSynced()) {
        //std::vector<CNode*> vNodesCopy = connman.CopyNodeVector();
        //governance.RequestGovernanceObjectVotes(vNodesCopy, connman);
        //connman.ReleaseNodeVector(vNodesCopy);
        return;
    }

    // Calculate "progress" for LOG reporting / GUI notification
    double nSyncProgress = double(nRequestedSmartnodeAttempt + (nRequestedSmartnodeAssets - 1) * 8) / (8*4);
    LogPrintf("CSmartnodeSync::ProcessTick -- nTick %d nRequestedSmartnodeAssets %d nRequestedSmartnodeAttempt %d nSyncProgress %f\n", nTick, nRequestedSmartnodeAssets, nRequestedSmartnodeAttempt, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);

    if( nRequestedSmartnodeAssets == SMARTNODE_SYNC_LIST ){
        LogPrintf("CSmartnodeSync::ProcessTick -- mean node count: %d, received nodes %d\n", GetMeanListCount(), mnodeman.size());
    }

    int nMinProtocol = mnpayments.GetMinSmartnodePaymentsProto();
    int nMinProtocolFound = 0;

    std::vector<CNode*> vNodesCopy = connman.CopyNodeVector(CConnman::FullyConnectedOnly);

    BOOST_FOREACH(CNode* pnode, vNodesCopy)
    {
        // Don't try to sync any data from outbound "smartnode" connections -
        // they are temporary and should be considered unreliable for a sync process.
        // Inbound connection this early is most likely a "smartnode" connection
        // initiated from another node, so skip it too.
        if(pnode->fSmartnode || (fSmartNode && pnode->fInbound) || pnode->nVersion < nMinProtocol ) continue;

        if( pnode->nVersion >= nMinProtocol ){
            ++nMinProtocolFound;
            LogPrint("mnsync", "CSmartnodeSync::ProcessTick -- New node found - %d!\n", nMinProtocolFound);
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if(netfulfilledman.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogPrintf("CSmartnodeSync::ProcessTick -- disconnecting from recently synced peer %d\n", pnode->id);
                continue;
            }

            // INITIAL TIMEOUT

            if(nRequestedSmartnodeAssets == SMARTNODE_SYNC_WAITING) {
                if(GetTime() - nTimeLastBumped > SMARTNODE_SYNC_TIMEOUT_SECONDS) {
                    // At this point we know that:
                    // a) there are peers (because we are looping on at least one of them);
                    // b) we waited for at least SMARTNODE_SYNC_TIMEOUT_SECONDS since we reached
                    //    the headers tip the last time (i.e. since we switched from
                    //     SMARTNODE_SYNC_INITIAL to SMARTNODE_SYNC_WAITING and bumped time);
                    // c) there were no blocks (UpdatedBlockTip, NotifyHeaderTip) or headers (AcceptedBlockHeader)
                    //    for at least SMARTNODE_SYNC_TIMEOUT_SECONDS.
                    // We must be at the tip already, let's move to the next asset.
                    SwitchToNextAsset(connman);
                }
            }

            // MNLIST : SYNC SMARTNODE LIST FROM OTHER CONNECTED CLIENTS

            if(nRequestedSmartnodeAssets == SMARTNODE_SYNC_LIST) {

                int nMeanListCount = GetMeanListCount();
                bool fEnoughNodes = (nMeanListCount && mnodeman.size() >= nMeanListCount * 0.9) || ( !nMeanListCount && nRequestedSmartnodeAttempt > 1 );

                LogPrint("mnsync", "CSmartnodeSync::ProcessTick -- nTick %d nRequestedSmartnodeAssets %d nTimeLastBumped %lld GetTime() %lld diff %lld\n", nTick, nRequestedSmartnodeAssets, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                if( ( GetTime() - nTimeLastBumped > SMARTNODE_SYNC_TIMEOUT_SECONDS && fEnoughNodes ) ||
                   GetTime() - nTimeLastBumped > SMARTNODE_SYNC_TIMEOUT_SECONDS * 8 ) {

                    LogPrintf("CSmartnodeSync::ProcessTick -- nTick %d nRequestedSmartnodeAssets %d -- timeout\n", nTick, nRequestedSmartnodeAssets);
                    if (nRequestedSmartnodeAttempt == 0 || !fEnoughNodes ) {
                        LogPrintf("CSmartnodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // there is no way we can continue without smartnode list, fail here and try later
                        Fail(connman);
                        connman.ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // request from three peers max
                if (nRequestedSmartnodeAttempt > 2) {
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "smartnode-list-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "smartnode-list-sync");

                if (pnode->nVersion < mnpayments.GetMinSmartnodePaymentsProto()) continue;
                nRequestedSmartnodeAttempt++;

                mnodeman.DsegUpdate(pnode, connman);

                connman.ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // MNW : SYNC SMARTNODE PAYMENT VOTES FROM OTHER CONNECTED CLIENTS

            if(nRequestedSmartnodeAssets == SMARTNODE_SYNC_MNW) {
                LogPrint("mnpayments", "CSmartnodeSync::ProcessTick -- nTick %d nRequestedSmartnodeAssets %d nTimeLastBumped %lld GetTime() %lld diff %lld\n", nTick, nRequestedSmartnodeAssets, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                // This might take a lot longer than SMARTNODE_SYNC_TIMEOUT_SECONDS due to new blocks,
                // but that should be OK and it should timeout eventually.
                if(GetTime() - nTimeLastBumped > SMARTNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CSmartnodeSync::ProcessTick -- nTick %d nRequestedSmartnodeAssets %d -- timeout\n", nTick, nRequestedSmartnodeAssets);
                    if (nRequestedSmartnodeAttempt == 0) {
                        LogPrintf("CSmartnodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // probably not a good idea to proceed without winner list
                        Fail(connman);
                        connman.ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // check for data
                // if mnpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if(nRequestedSmartnodeAttempt > 1 && mnpayments.IsEnoughData()) {
                    LogPrintf("CSmartnodeSync::ProcessTick -- nTick %d nRequestedSmartnodeAssets %d -- found enough data\n", nTick, nRequestedSmartnodeAssets);
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "smartnode-payment-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "smartnode-payment-sync");

                nRequestedSmartnodeAttempt++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                connman.PushMessage(pnode, NetMsgType::SMARTNODEPAYMENTSYNC, mnpayments.GetStorageLimit());
                // ask node for missing pieces only (old nodes will not be asked)
                mnpayments.RequestLowDataPaymentBlocks(pnode, connman);

                connman.ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }
        }
    }

    // If no new protocol node was there after new collateral got enabled
    // disconnect half of the nodes and try to find one
    if(!nMinProtocolFound){
        LogPrint("mnsync", "CSmartnodeSync::ProcessTick -- NO new node found!\n");

        if(vNodesCopy.size() < 4){
            int nDisconnect = vNodesCopy.size() / 2;
            LogPrint("mnsync", "CSmartnodeSync::ProcessTick -- Disconnect %i nodes!\n", nDisconnect);
            Disconnect(nDisconnect, nMinProtocol);
        }
    }

    // looped through all nodes, release them
    connman.ReleaseNodeVector(vNodesCopy);
}

void CSmartnodeSync::AcceptedBlockHeader(const CBlockIndex *pindexNew)
{
    LogPrint("mnsync", "CSmartnodeSync::AcceptedBlockHeader -- pindexNew->nHeight: %d\n", pindexNew->nHeight);

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block header arrives while we are still syncing blockchain
        BumpAssetLastTime("CSmartnodeSync::AcceptedBlockHeader");
    }
}

void CSmartnodeSync::NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman)
{
    LogPrint("mnsync", "CSmartnodeSync::NotifyHeaderTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CSmartnodeSync::NotifyHeaderTip");
    }
}

void CSmartnodeSync::UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman)
{
    LogPrint("mnsync", "CSmartnodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CSmartnodeSync::UpdatedBlockTip");
    }

    if (fInitialDownload) {
        // switched too early
        if (IsBlockchainSynced()) {
            LogPrint("mnsync", "CSmartnodeSync::UpdatedBlockTip -- Reset, switched too early!\n");
            Reset();
        }

        // no need to check any further while still in IBD mode
        return;
    }

    // Note: since we sync headers first, it should be ok to use this
    static bool fReachedBestHeader = false;
    bool fReachedBestHeaderNew = pindexNew->GetBlockHash() == pindexBestHeader->GetBlockHash();

    if (fReachedBestHeader && !fReachedBestHeaderNew) {
        // Switching from true to false means that we previousely stuck syncing headers for some reason,
        // probably initial timeout was not enough,
        // because there is no way we can update tip not having best header
        Reset();
        LogPrint("mnsync", "CSmartnodeSync::UpdatedBlockTip -- Reset, stucked on header sync?\n");
        fReachedBestHeader = false;
        return;
    }

    fReachedBestHeader = fReachedBestHeaderNew;

    LogPrint("mnsync", "CSmartnodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d pindexBestHeader->nHeight: %d fInitialDownload=%d fReachedBestHeader=%d\n",
                pindexNew->nHeight, pindexBestHeader->nHeight, fInitialDownload, fReachedBestHeader);

    if (!IsBlockchainSynced() && fReachedBestHeader) {
        // Reached best header while being in initial mode.
        // We must be at the tip already, let's move to the next asset.
        SwitchToNextAsset(connman);
    }
}
