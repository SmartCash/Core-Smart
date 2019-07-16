// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "dsnotificationinterface.h"
#include "smartnode/instantx.h"
#include "smartnode/smartnodeman.h"
#include "smartnode/smartnodepayments.h"
#include "smartnode/smartnodesync.h"
#include "smartvoting/manager.h"

void CDSNotificationInterface::InitializeCurrentBlockTip()
{
    LOCK(cs_main);
    UpdatedBlockTip(chainActive.Tip(), NULL, IsInitialBlockDownload());
}

void CDSNotificationInterface::AcceptedBlockHeader(const CBlockIndex *pindexNew)
{
    smartnodeSync.AcceptedBlockHeader(pindexNew);
}

void CDSNotificationInterface::NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload)
{
    smartnodeSync.NotifyHeaderTip(pindexNew, fInitialDownload, connman);
}

void CDSNotificationInterface::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload)
{
    if (pindexNew == pindexFork) // blocks were disconnected without any new ones
        return;

    smartnodeSync.UpdatedBlockTip(pindexNew, fInitialDownload, connman);

    // Update global DIP0001 activation status
    //fDIP0001ActiveAtTip = (VersionBitsState(pindexNew, Params().GetConsensus(), Consensus::DEPLOYMENT_DIP0001, versionbitscache) == THRESHOLD_ACTIVE);

    if (fInitialDownload)
        return;

    mnodeman.UpdatedBlockTip(pindexNew);
    instantsend.UpdatedBlockTip(pindexNew);
    mnpayments.UpdatedBlockTip(pindexNew, connman);

// WIP-VOTING uncomment
//    smartVoting.UpdatedBlockTip(pindexNew, connman);
}

void CDSNotificationInterface::SyncTransaction(const CTransaction &tx, const CBlock *pblock)
{
    instantsend.SyncTransaction(tx, pblock);
    //CPrivateSend::SyncTransaction(tx, pblock);
}
