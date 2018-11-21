// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "votevalidation.h"

#include "init.h"
#include "smartnode/smartnodesync.h"
#include "smartvoting/manager.h"
#include "spentindex.h"
#include "validation.h"

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

    while (true)
    {
        MilliSleep(1000);

        if(smartnodeSync.IsSynced() && !ShutdownRequested()) {

            int nHeight = chainActive.Height();

            if( nHeight < nValidationConfirmations )
                continue;

            nHeight -= nValidationConfirmations;

            if( nHeight % nValidationInterval )
                continue;

            // First add all missing active addresses
            std::set<CVoteKey> setActiveKeys;
            std::vector<const CProposal*> vecProposals = smartVoting.GetAllNewerThan(0);

            for( auto proposal : vecProposals ){
                proposal->GetActiveVoteKeys(setActiveKeys);
            }

            for( auto it : setActiveKeys ){
                AddActiveVoteKey(it);
            }

            LOCK(cs);

            for (auto it = mapActiveVoteKeys.begin(); it != mapActiveVoteKeys.end();){

                // Check if the address we validate is not longer active in any proposal
                if( !setActiveKeys.count(it->first) ){
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
    }else{
        votingPower.SetNull();
    }
}

CAmount GetVotingPower(const CVoteKey &voteKey)
{
    LOCK(cs);
    auto it = mapActiveVoteKeys.find(voteKey);

    if( it != mapActiveVoteKeys.end() && it->second.IsValid() ){
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
