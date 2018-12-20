// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "votevalidation.h"

#include "init.h"
#include "smartnode/smartnodesync.h"
#include "smartvoting/manager.h"
#include "spentindex.h"
#include "validation.h"
#include "wallet/wallet.h"
#include "txdb.h"

static CCriticalSection cs;

static std::map<CVoteKey, CVotingPower> mapActiveVoteKeys;

bool GetBalanceDelta(const CSmartAddress &address, int nStartBlock, int nEndBlock, CAmount &delta);

void ThreadSmartVoting()
{
    static bool fOneThread;
    if(fOneThread) return;
    fOneThread = true;

    // We don't need to calculate any voting power in litemode.
    if( fLiteMode ) return;

    // Make this thread recognisable as the SmartVoting thread
    RenameThread("smartvoting");

    // Check if we have some unparsed votekey registrations every block
    int nLastChecked = 0;

    while (true)
    {
        MilliSleep(1000);

        if( ShutdownRequested() ) return;

        if(smartnodeSync.IsBlockchainSynced() ) {

            int nHeight = chainActive.Height();

            if( nHeight == nLastChecked ) continue;

            nLastChecked = nHeight;

            std::set<CVoteKey> setActiveKeys;

            // Update votekeys active from proposals
            if( smartnodeSync.IsSynced() ){

                std::vector<const CProposal*> vecProposals = smartVoting.GetAllNewerThan(0);

                for( auto proposal : vecProposals ){
                    proposal->GetActiveVoteKeys(setActiveKeys);
                }

                for( auto it : setActiveKeys ){
                    AddActiveVoteKey(it);
                }

            }

            if( pwalletMain ){

                // Add votekeys available in the wallet to the validation
                // and update the meta data of the votekeys if necessary

                std::set<CKeyID> setWalletKeyIds;

                {
                    LOCK(pwalletMain->cs_wallet);
                    pwalletMain->GetVotingKeys(setWalletKeyIds);
                }

                for( auto keyId : setWalletKeyIds ){

                    CVoteKey voteKey(keyId);
                    CVoteKeyValue value;

                    if( IsRegisteredForVoting(voteKey) ){

                        if( !setActiveKeys.count(voteKey) )
                            setActiveKeys.insert(voteKey);

                        AddActiveVoteKey(voteKey);
                    }
                }

            }

            LOCK(cs);

            for (auto it = mapActiveVoteKeys.begin(); it != mapActiveVoteKeys.end();){

                // Check if the address we validate is not longer active
                if( setActiveKeys.size() && !setActiveKeys.count(it->first) ){
                    it = mapActiveVoteKeys.erase(it);
                    continue;
                }

                int nStart = 0;

                if( it->second.IsValid() ){

                    if( it->second.nBlockHeight < nHeight ){
                        nStart = it->second.nBlockHeight + 1;
                    }else{
                        ++it;
                        continue;
                    }

                }

                if( nHeight < nValidationConfirmations ){
                    ++it;
                    continue;
                }

                if( ( nHeight - nStart ) < nValidationConfirmations ){
                    ++it;
                    continue;
                }

                CAmount nDelta = 0;

                if( GetBalanceDelta(it->second.address, nStart, nHeight, nDelta) ){
                    it->second.nPower += nDelta;
                    it->second.nBlockHeight = nHeight;
                }

                ++it;

            }
        }
    }
}

bool GetBalanceDelta(const CSmartAddress &address, int nStartBlock, int nEndBlock, CAmount &delta)
{
    uint160 hashBytes;
    int type = 0;

    if (!address.GetIndexKey(hashBytes, type)) {
        return false;
    }

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;

    if ( !GetAddressIndex(hashBytes, type, addressIndex, nStartBlock, nEndBlock) ) {
        return false;
    }

    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin(); it!=addressIndex.end(); it++)
        delta += it->second;

    return true;
}

void GetVotingPower(const CVoteKey &voteKey, CVotingPower &votingPower)
{
    LOCK(cs);
    auto it = mapActiveVoteKeys.find(voteKey);

    if( it != mapActiveVoteKeys.end() && it->second.IsValid() ){
        votingPower = it->second;
        votingPower.nPower /= COIN;
    }else{
        votingPower.SetNull();
    }
}

int64_t GetVotingPower(const CVoteKey &voteKey)
{
    LOCK(cs);
    auto it = mapActiveVoteKeys.find(voteKey);

    if( it != mapActiveVoteKeys.end() && it->second.IsValid() ){

        if( it->second.nPower > 0 )
            return it->second.nPower / COIN;
        else
            return it->second.nPower;
    }

    return 0;
}

void AddActiveVoteKey(const CVoteKey &voteKey)
{
    LOCK(cs);

    if( !mapActiveVoteKeys.count(voteKey) ){
        CVoteKeyValue voteKeyValue;
        if(GetVoteKeyValue(voteKey, voteKeyValue) )
            mapActiveVoteKeys.insert(std::make_pair(voteKey, CVotingPower(voteKeyValue.voteAddress)));
    }
}
