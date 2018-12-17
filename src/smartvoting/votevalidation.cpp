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
static std::map<uint256, std::pair<int,int>> mapVoteKeyRegistrations;

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

        int nHeight = chainActive.Height();

        if( nHeight == nLastChecked ) continue;

        nLastChecked = nHeight;

        std::vector<std::pair<CVoteKeyRegistrationKey, CVoteKeyRegistrationValue>> vecRegistrations;

        if( !pblocktree->ReadVoteKeyRegistrations(vecRegistrations) ){
            LogPrint("votekeys", "ThreadSmartVoting: Failed to read VoteKey registrations\n");
            continue;
        }

        for(auto reg : vecRegistrations){

            CVoteKeyRegistrationKey *pKey = &reg.first;

            if( !((nHeight - pKey->nHeight) % nRegistrationCheckInterval) ){

                uint256 blockHash;
                CTransaction rTx;

                CVoteKey voteKey;
                CSmartAddress voteAddress;

                if(!::GetTransaction(pKey->nTxHash, rTx, Params().GetConsensus(), blockHash)){
                    LogPrint("votekeys", "ThreadSmartVoting: GetTransaction failed - %s\n", pKey->nTxHash.ToString());
                    continue;
                }

                if( nHeight - pKey->nHeight < nValidationConfirmations ){
                    LogPrint("votekeys","ThreadSmartVoting: Not enough confirmations - %d\n", pKey->nTxHash.ToString());
                    continue;
                }

                VoteKeyParseResult result = ParseVoteKeyRegistration(rTx, voteKey, voteAddress);

                if( result != VoteKeyParseResult::Valid){

                    if( result == VoteKeyParseResult::TxResolveFailed ){
                        // If the tx for option 1 could not be found yet we want to try it again later
                        // since in some cases the tx index was not updated during tests when
                        // when the registration became parsed.
                        LogPrint("votekeys","ThreadSmartVoting: TxResolveFailed failed - %s\n", pKey->nTxHash.ToString());
                    }else{

                        // All other fails will end up in a invalidation of the registration tx
                        if (!pblocktree->InvalidateVoteKeyRegistration(pKey->nHeight, pKey->nTxHash)){
                            LogPrint("votekeys","ThreadSmartVoting: InvalidateVoteKeyRegistration failed - %s\n", pKey->nTxHash.ToString());
                        }
                    }

                    LogPrint("votekeys","ThreadSmartVoting: ParseVoteKeyRegistration failed - %s\n", rTx.ToString());
                    continue;
                }

                int nVoteKeyRegisteredHeight;
                int nVoteAddressRegisteredHeight;
                CVoteKey voteKeyRegisteredForAddress;

                bool fVoteKeyRegistered = IsRegisteredForVoting(voteKey, nVoteKeyRegisteredHeight);
                bool fVoteAddressRegistered = IsRegisteredForVoting(voteAddress, voteKeyRegisteredForAddress, nVoteAddressRegisteredHeight);

                CVoteKey removeKey;
                bool fInvalid = false;

                if( fVoteKeyRegistered && !fVoteAddressRegistered ){

                    LogPrint("votekeys","ThreadSmartVoting: VoteKey IsRegisteredForVoting - %s\n", voteKey.ToString());

                    if( nVoteKeyRegisteredHeight > pKey->nHeight ){
                        LogPrint("votekeys","ThreadSmartVoting: VoteKey IsRegisteredForVoting - Found an older registration\n");
                        removeKey = voteKey;
                    }else{
                        fInvalid = true;
                    }

                }else if( !fVoteKeyRegistered && fVoteAddressRegistered ){

                    if( nVoteAddressRegisteredHeight > pKey->nHeight ){
                        removeKey = voteKeyRegisteredForAddress;
                    }else{
                        fInvalid = true;
                    }

                }else if( fVoteKeyRegistered && fVoteAddressRegistered ){

                    // If the registered for the address differes from the one we try to register
                    // And the one we try to register is an older one
                    if( !( voteKeyRegisteredForAddress == voteKey ) &&
                         nVoteKeyRegisteredHeight > pKey->nHeight ){

                        removeKey = voteKeyRegisteredForAddress;
                    }else{
                        fInvalid = true;
                    }

                }

                if( removeKey.IsValid() ){
                    // First remove the wrong entry
                    if (!pblocktree->EraseVoteKeys({removeKey})){
                        LogPrint("votekeys","ThreadSmartVoting: EraseVoteKeys failed - %s\n", voteKey.ToString());
                    }

                    // Remove it from the active keys to force a revalidation after we wrote the new one below
                    LOCK(cs);
                    auto rm = mapActiveVoteKeys.find(removeKey);
                    if( rm != mapActiveVoteKeys.end() ) mapActiveVoteKeys.erase(rm);

                    continue;
                }

                if( fInvalid ){

                    if (!pblocktree->InvalidateVoteKeyRegistration(pKey->nHeight, pKey->nTxHash, voteKey)){
                        LogPrint("votekeys","ThreadSmartVoting: InvalidateVoteKeyRegistration failed - %s\n", pKey->nTxHash.ToString());
                    }

                    continue;
                }

                CVoteKeyValue voteKeyValue(voteAddress, rTx.GetHash(), pKey->nHeight);

                if (!pblocktree->WriteVoteKey(voteKey,voteKeyValue)){
                    LogPrint("votekeys","ThreadSmartVoting: WriteVoteKey failed - %s, %s\n", voteKey.ToString(), voteKeyValue.ToString());
                    continue;
                }

                LogPrint("votekeys","ThreadSmartVoting: New VoteKey registered tx=%s - %s - %s\n",rTx.GetHash().ToString(), voteKey.ToString(), voteAddress.ToString() );
            }

        }


        if(smartnodeSync.IsBlockchainSynced() ) {

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

                    CVoteKey vk(keyId);
                    CVoteKeyValue value;

                    // This one needs to become checked first
                    if( !pwalletMain->mapVotingKeyMetadata[keyId].fChecked ){

                        if( !GetVoteKeyValue(vk, value) ){

                            bool fInvalidate = true;
                            uint256 &txHash = pwalletMain->mapVotingKeyMetadata[keyId].registrationTxHash;

                            if( !txHash.IsNull() ){

                                fInvalidate = false;

                                uint256 blockHash;
                                CTransaction rTx;

                                if(::GetTransaction(txHash, rTx, Params().GetConsensus(), blockHash)){

                                    if(blockHash != uint256()) {

                                        int nConfirmations = 0;

                                        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
                                        if (mi != mapBlockIndex.end() && (*mi).second) {
                                            CBlockIndex* pindex = (*mi).second;
                                            if (chainActive.Contains(pindex)) {
                                                nConfirmations += chainActive.Height() - pindex->nHeight + 1;
                                            }
                                        }

                                        if( nConfirmations > (nValidationConfirmations * 2 )){
                                            fInvalidate = true;
                                        }

                                    }else{
                                        LogPrint("votekeys", "ThreadSmartVoting: Registration not mined yet - %s\n", txHash.ToString());
                                    }
                                }else{
                                    LogPrint("votekeys", "ThreadSmartVoting: GetTransaction failed for wallet check - %s\n", txHash.ToString());
                                }
                            }

                            if( fInvalidate ){
                                pwalletMain->mapVotingKeyMetadata[keyId].fChecked = false;
                                pwalletMain->mapVotingKeyMetadata[keyId].fValid = false;
                                pwalletMain->mapVotingKeyMetadata[keyId].fEnabled = false;
                                pwalletMain->mapVotingKeyMetadata[keyId].registrationTxHash.SetNull();
                                LOCK(pwalletMain->cs_wallet);
                                pwalletMain->UpdateVotingKeyMetadata(keyId);
                            }

                            continue;
                        }

                        pwalletMain->mapVotingKeyMetadata[keyId].fChecked = true;
                        pwalletMain->mapVotingKeyMetadata[keyId].fValid = true;
                        pwalletMain->mapVotingKeyMetadata[keyId].fEnabled = true;
                        pwalletMain->mapVotingKeyMetadata[keyId].registrationTxHash = value.nTxHash;
                        LOCK(pwalletMain->cs_wallet);
                        pwalletMain->UpdateVotingKeyMetadata(keyId);
                    }

                    if( pwalletMain->mapVotingKeyMetadata[keyId].fValid ){

                        if( !setActiveKeys.count(vk) )
                            setActiveKeys.insert(vk);

                        AddActiveVoteKey(vk);

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
