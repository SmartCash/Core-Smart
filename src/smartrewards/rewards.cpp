// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/consensus.h"
#include "init.h"
#include "smartrewards/rewards.h"
#include "smartrewards/rewardspayments.h"
#include "smarthive/hive.h"
#include "smartnode/spork.h"
#include "smartnode/smartnodepayments.h"
#include "ui_interface.h"
#include "validation.h"

#include <boost/thread.hpp>
#include <boost/range/irange.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>

CSmartRewards *prewards = NULL;

CCriticalSection cs_rewardsdb;
CCriticalSection cs_rewardrounds;

// Used for time conversions.
boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));

// Estimate or return the current block height.
int GetBlockHeight(const CBlockIndex *index)
{
    int64_t syncDiff = std::time(0) - index->GetBlockTime();
    int64_t firstTxDiff;
    if( MainNet() ) firstTxDiff = std::time(0) - nStartRewardTime; // Diff from the reward blocks start till now on mainnet.
    else firstTxDiff = std::time(0) - nFirstTxTimestamp_Testnet; // Diff from the first transaction till now on testnet.
    return syncDiff > 1200 ? firstTxDiff / 55 : index->nHeight; // If we are 20 minutes near now use the current height.
}

int ParseScript(const CScript &script, std::vector<CSmartAddress> &ids){

    std::vector<CTxDestination> addresses;
    txnouttype type;
    int nRequired;

    if (!ExtractDestinations(script, type, addresses, nRequired)) {
        return 0;
    }

    BOOST_FOREACH(const CTxDestination &d, addresses)
    {
        ids.push_back(CSmartAddress(d));
    }

    return nRequired;
}

void CalculateRewardRatio(CSmartRewardRound &round)
{
    int64_t time = GetTime();
    int64_t start = round.startBlockHeight;
    round.rewards = 0;

    while( start <= round.endBlockHeight) round.rewards += GetBlockValue(start++,0,time) * 0.15;

    round.percent = double(round.rewards) / ( round.eligibleSmart - round.disqualifiedSmart );
}

bool CSmartRewards::Verify()
{
    LOCK(cs_rewardsdb);
    return pdb->Verify(rewardHeight);
}


void CSmartRewards::UpdatePayoutParameter(CSmartRewardRound &round)
{

    int64_t nPayeeCount = round.eligibleEntries - round.disqualifiedEntries;
    int nFirst_1_3_Round = MainNet() ? nRewardsFirst_1_3_Round : nRewardsFirst_1_3_Round_Testnet;

    if( round.number < nFirst_1_3_Round ){

        round.nBlockPayees = nRewardPayouts_1_2_BlockPayees;
        round.nBlockInterval = nRewardPayouts_1_2_BlockInterval;

        if( TestNet() ){
            if( round.number < 68 ){
                round.nBlockPayees = nRewardPayoutsPerBlock_1_Testnet;
                round.nBlockInterval = nRewardPayoutBlockInterval_1_Testnet;
            }else{
                round.nBlockPayees = nRewardPayoutsPerBlock_2_Testnet;
                round.nBlockInterval = nRewardPayoutBlockInterval_2_Testnet;
            }
        }

    }else{

        int64_t nBlockStretch = MainNet() ? nRewardPayouts_1_3_BlockStretch :
                                   nRewardPayouts_1_3_BlockStretch_Testnet;
        int64_t nBlocksPerRound = MainNet() ? nRewardsBlocksPerRound_1_3 :
                                   nRewardsBlocksPerRound_1_3_Testnet;
        int64_t nBlockPayees = MainNet() ? nRewardPayouts_1_3_BlockPayees :
                                           nRewardPayouts_1_3_BlockPayees_Testnet;

        round.nBlockPayees = std::max<int>(nBlockPayees, (nPayeeCount / nBlockStretch * nBlockPayees) + 1);

        int64_t nStartDelayBlocks = MainNet() ? nRewardPayoutStartDelay : nRewardPayoutStartDelay_Testnet;
        int64_t nBlocksTarget = nStartDelayBlocks + nBlocksPerRound;
        round.nBlockInterval = ((nBlockStretch * round.nBlockPayees) / nPayeeCount) + 1;
        int64_t nStretchedLength = nPayeeCount / round.nBlockPayees * (round.nBlockInterval);

        if( nStretchedLength > nBlocksTarget ){
            round.nBlockInterval--;
        }else if( nStretchedLength < nBlockStretch ){
            round.nBlockInterval++;
        }
    }
}

void CSmartRewards::EvaluateRound(CSmartRewardRound &current, CSmartRewardRound &next, CSmartRewardEntryList &entries, CSmartRewardSnapshotList &snapshots)
{
    LOCK(cs_rewardsdb);
    snapshots.clear();

    UpdatePayoutParameter(current);

    BOOST_FOREACH(CSmartRewardEntry &entry, entries) {

        if( current.number ) snapshots.push_back(CSmartRewardSnapshot(entry, current));

        entry.balanceOnStart = entry.balance;
        // Reset SmartNode flag with every cycle in case a node was shut down during the cycle.
        entry.fIsSmartNode = false;
        // Reset the voted flag with every cycle to force a new vote for eligibility
        entry.fVoteProved = false;
        // Evaluate the balance eligibilty
        entry.fBalanceEligible = entry.balanceOnStart >= SMART_REWARDS_MIN_BALANCE && !SmartHive::IsHive(entry.id);

        if( entry.IsEligible() ){
            ++next.eligibleEntries;
            next.eligibleSmart += entry.balanceOnStart;
        }
    }
}

bool CSmartRewards::StartFirstRound(const CSmartRewardRound &first, const CSmartRewardEntryList &entries)
{
    LOCK(cs_rewardsdb);
    return pdb->StartFirstRound(first,entries);
}

bool CSmartRewards::FinalizeRound(const CSmartRewardRound &current, const CSmartRewardRound &next, const CSmartRewardEntryList &entries, const CSmartRewardSnapshotList &snapshots)
{
    LOCK(cs_rewardsdb);
    return pdb->FinalizeRound(current, next, entries, snapshots);
}

bool CSmartRewards::GetRewardSnapshots(const int16_t round, CSmartRewardSnapshotList &snapshots)
{
    LOCK(cs_rewardsdb);
    return pdb->ReadRewardSnapshots(round, snapshots);
}

bool CSmartRewards::GetRewardPayouts(const int16_t round, CSmartRewardSnapshotList &payouts)
{
    LOCK(cs_rewardsdb);
    return pdb->ReadRewardPayouts(round, payouts);
}

bool CSmartRewards::GetRewardPayouts(const int16_t round, CSmartRewardSnapshotPtrList &payouts)
{
    LOCK(cs_rewardsdb);
    return pdb->ReadRewardPayouts(round, payouts);
}

bool CSmartRewards::GetCachedRewardEntry(const CSmartAddress &id, CSmartRewardEntry *&entry)
{
    LOCK(cs_rewardsdb);

    // Return the entry if its already in cache.
    auto findResult = rewardEntries.find(id);

    if( findResult != rewardEntries.end() ){
        entry = findResult->second;
        return true;
    }

    return false;
}

bool CSmartRewards::ReadRewardEntry(const CSmartAddress &id, CSmartRewardEntry &entry)
{
    LOCK(cs_rewardsdb);
    return pdb->ReadRewardEntry(id,entry);
}

bool CSmartRewards::GetRewardEntry(const CSmartAddress &id, CSmartRewardEntry &entry)
{
    LOCK(cs_rewardsdb);

    CSmartRewardEntry * pReadEntry;

    if( GetCachedRewardEntry(id,pReadEntry) ){
        entry = *pReadEntry;
        return true;
    }

    return ReadRewardEntry(id,entry);
}

bool CSmartRewards::GetRewardEntries(CSmartRewardEntryList &entries)
{
    LOCK(cs_rewardsdb);
    return pdb->ReadRewardEntries(entries);
}

bool CSmartRewards::SyncPrepared()
{
    LOCK2(cs_rewardsdb, cs_rewardrounds);

    bool ret =  pdb->SyncBlocks(blockEntries,currentRound, rewardEntries, transactionEntries);

    for( std::pair<CSmartAddress, CSmartRewardEntry*> it : rewardEntries ){
        delete it.second;
    }

    rewardEntries.clear();
    blockEntries.clear();
    transactionEntries.clear();

    return ret;
}

bool CSmartRewards::IsSynced()
{
    int nSyncDistance = MainNet() ? nRewardsSyncDistance : nRewardsSyncDistance_Testnet;
    return (chainHeight - rewardHeight) <= nSyncDistance - 1;
}

double CSmartRewards::GetProgress()
{
    int nSyncDistance = MainNet() ? nRewardsSyncDistance : nRewardsSyncDistance_Testnet;
    double progress = chainHeight > nSyncDistance ? double(rewardHeight) / double(chainHeight - nSyncDistance) : 0.0;
    return progress > 1 ? 1 : progress;
}

int CSmartRewards::GetLastHeight()
{
    return rewardHeight;
}

int CSmartRewards::GetBlocksPerRound(const int nRound)
{
    LOCK(cs_rewardrounds);

    if( MainNet() ){

        if( nRound < nRewardsFirst_1_3_Round )
            return nRewardsBlocksPerRound_1_2;
        else
            return nRewardsBlocksPerRound_1_3;

    }else{

        if( nRound < nRewardsFirst_1_3_Round_Testnet )
            return nRewardsBlocksPerRound_1_2_Testnet;
        else
            return nRewardsBlocksPerRound_1_3_Testnet;

    }

}

bool CSmartRewards::AddBlock(const CSmartRewardBlock &block, bool sync)
{
    blockEntries.push_back(block);

    int nTime1 = GetTimeMicros();

    if(sync) return SyncPrepared();

    int nTimeSync = GetTimeMicros() - nTime1;
    if( LogAcceptCategory("smartrewards-block")){
        LogPrint("smartrewards-block", "CSmartRewards::AddBlock - Sync - Block: %d - Progress %.2fms\n",block.nHeight, nTimeSync * 0.001);
    }

    return true;
}

void CSmartRewards::AddTransaction(const CSmartRewardTransaction &transaction)
{
    LOCK(cs_rewardsdb);
    transactionEntries.push_back(transaction);
}

CSmartRewards::CSmartRewards(CSmartRewardsDB *prewardsdb)  : pdb(prewardsdb)
{
    LOCK(cs_rewardsdb);

    // Get the last written block of the rewards database.
    if(!pdb->ReadLastBlock(currentBlock)){
        // If there is no one available yet
        // Use 0 to get 1 as start below.
        currentBlock.nHeight = 0;
        currentBlock.blockHash = uint256();
        currentBlock.blockTime = 0;
    }

    pdb->ReadRounds(finishedRounds);

    std::sort(finishedRounds.begin(), finishedRounds.end());

    if( finishedRounds.size() ){
        lastRound = finishedRounds.back();
    }

    pdb->ReadCurrentRound(currentRound);
}

void CSmartRewards::Lock()
{
    LOCK(cs_rewardsdb);
    pdb->Lock();
}

bool CSmartRewards::IsLocked()
{
    LOCK(cs_rewardsdb);
    return pdb->IsLocked();
}

bool CSmartRewards::GetLastBlock(CSmartRewardBlock &block)
{
    LOCK(cs_rewardsdb);
    // Read the last block stored in the rewards database.
    return pdb->ReadLastBlock(block);
}

bool CSmartRewards::GetTransaction(const uint256 hash, CSmartRewardTransaction &transaction)
{
    // If the transaction is already in the cache use this one.
    BOOST_FOREACH(CSmartRewardTransaction t, transactionEntries) {
        if(t.hash == hash){
            transaction = t;
            return true;
        }
    }

    return pdb->ReadTransaction(hash, transaction);
}

const CSmartRewardRound& CSmartRewards::GetCurrentRound()
{
    return currentRound;
}

const CSmartRewardRound& CSmartRewards::GetLastRound()
{
    return lastRound;
}

const CSmartRewardRoundList& CSmartRewards::GetRewardRounds()
{
    return finishedRounds;
}

void CSmartRewards::UpdateHeights(const int nHeight, const int nRewardHeight)
{
    chainHeight = nHeight;
    rewardHeight = nRewardHeight;
}

void CSmartRewards::ProcessTransaction(CBlockIndex* pIndex, const CTransaction& tx, CCoinsViewCache& coins, const CChainParams& chainparams, CSmartRewardsUpdateResult &result)
{

    CSmartRewardEntry *rEntry = nullptr;

    int nFirst1_3_Round = MainNet() ? nRewardsFirst_1_3_Round : nRewardsFirst_1_3_Round_Testnet;

    int nCurrentRound;

    {
        LOCK(cs_rewardrounds);
        nCurrentRound = currentRound.number;
    }

    int nHeight = pIndex->nHeight;

    if(nHeight > sporkManager.GetSporkValue(SPORK_15_SMARTREWARDS_BLOCKS_ENABLED)){
        return;
    }

    int nTime1 = GetTimeMicros();

    // Process the transaction!
    CSmartRewardTransaction testTx;

    // First check if the transaction hash did already come up in the past.
    if( GetTransaction(tx.GetHash(), testTx)){
        // If yes we want to ignore it!
        LogPrintf("CSmartRewards::ProcessTransaction - [%s] Double appearance! First in %d - Now in %d\n", testTx.hash.ToString(), testTx.blockHeight, pIndex->nHeight);
        return;
    }else{
        // If not save add it to the database.
        CSmartRewardTransaction saveTx(pIndex->nHeight, tx.GetHash());
        AddTransaction(saveTx);
    }

    CSmartAddress *voteKeyRegistrationCheck = nullptr;

    // No reason to check the input here for new coins.
    if( !tx.IsCoinBase() ){

        BOOST_FOREACH(const CTxIn &in, tx.vin) {

            if( in.scriptSig.IsZerocoinSpend() ) continue;

            const Coin &coin = coins.AccessCoin(in.prevout);
            const CTxOut &rOut = coin.out;

            std::vector<CSmartAddress> ids;

            int required = ParseScript(rOut.scriptPubKey ,ids);

            if( !required || required > 1 || ids.size() > 1 ){
                LogPrint("smartrewards-tx", "CSmartRewards::ProcessTransaction - Process Inputs: Could't parse CSmartAddress: %s\n",rOut.ToString());
                continue;
            }

            if(!GetCachedRewardEntry(ids.at(0),rEntry)){

                rEntry = new CSmartRewardEntry(ids.at(0));

                if(!ReadRewardEntry(rEntry->id, *rEntry)){
                    delete rEntry;
                    LogPrintf("%s: Spend without previous receive - %s", __func__, tx.ToString());
                    continue;
                }

                rewardEntries.insert(make_pair(ids.at(0), rEntry));
            }

            rEntry->balance -= rOut.nValue;

            if( rEntry->IsEligible() ){

                // If its a votekey registration not instantly make the
                // balance ineligible. First check if the change is sent back
                // to the address or not to avoid exploiting fund sending
                // with votekey registration transactions
                if( nCurrentRound >= nFirst1_3_Round && tx.IsVoteKeyRegistration()  )
                    voteKeyRegistrationCheck = new CSmartAddress(rEntry->id);
                else{
                    rEntry->fBalanceEligible = false;
                    result.disqualifiedEntries++;
                    result.disqualifiedSmart += rEntry->balanceOnStart;
                }

            }

            if(rEntry->balance < 0 ){
                LogPrintf("%s: Negative amount?! - %s", __func__, rEntry->ToString());
                rEntry->balance = 0;
            }

        }
    }

    int nTime2 = GetTimeMicros();

    BOOST_FOREACH(const CTxOut &out, tx.vout) {

        if(out.scriptPubKey.IsZerocoinMint() ) continue;

        std::vector<CSmartAddress> ids;
        int required = ParseScript(out.scriptPubKey ,ids);

        if( !required || required > 1 || ids.size() > 1 ){
            LogPrint("smartrewards-tx", "CSmartRewards::ProcessTransaction - Process Outputs: Could't parse CSmartAddress: %s\n",out.ToString());
            continue;
        }else{

            if(!GetCachedRewardEntry(ids.at(0),rEntry)){
                rEntry = new CSmartRewardEntry(ids.at(0));
                ReadRewardEntry(rEntry->id, *rEntry);
                rewardEntries.insert(make_pair(ids.at(0), rEntry));
            }

            if( voteKeyRegistrationCheck ){

                if( tx.IsVoteKeyRegistration() &&
                    !out.IsVoteKeyRegistrationData() &&
                    !(*voteKeyRegistrationCheck == rEntry->id) ){

                    CSmartRewardEntry *vkEntry = nullptr;

                    if(!GetCachedRewardEntry(*voteKeyRegistrationCheck,vkEntry)){
                        vkEntry = new CSmartRewardEntry(*voteKeyRegistrationCheck);
                        ReadRewardEntry(vkEntry->id, *vkEntry);
                        rewardEntries.insert(make_pair(vkEntry->id, vkEntry));
                    }

                    // Finally invalidate the balance since the change output went not
                    // back to the registered transaction! We don't want to allow
                    // a exploit to send around funds withouht breaking smartrewards.
                    vkEntry->fBalanceEligible = false;
                    result.disqualifiedEntries++;
                    result.disqualifiedSmart += vkEntry->balanceOnStart;
                }

                delete voteKeyRegistrationCheck;
            }

            rEntry->balance += out.nValue;

            // If we are in the 1.3 cycles check for node rewards to remove node addresses from lists
            if( nCurrentRound >= nFirst1_3_Round && tx.IsCoinBase() ){

                int nInterval = SmartNodePayments::PayoutInterval(nHeight);
                int nPayoutsPerBlock = SmartNodePayments::PayoutsPerBlock(nHeight);
                // Just to avoid potential zero divisions
                nPayoutsPerBlock = std::max(1,nPayoutsPerBlock);

                CAmount nNodeReward = SmartNodePayments::Payment(nHeight) / nPayoutsPerBlock;

                // If we have an interval check if this is a node payout block
                if( nInterval && !(nHeight % nInterval) ){

                    // If the amount matches and the entry is not yet marked as node do it
                    if( abs(out.nValue - nNodeReward ) < 2 && !rEntry->fIsSmartNode ){

                        // If it is currently eligible adjust the round's results
                        if( rEntry->IsEligible() ){
                            result.disqualifiedEntries++;
                            result.disqualifiedSmart += rEntry->balanceOnStart;
                        }

                        rEntry->fIsSmartNode = true;
                    }
                }
            }
        }
    }

    int nTime3 = GetTimeMicros();
    int nTimeTx = nTime3 - nTime1;

    if( LogAcceptCategory("smartrewards-tx") ){
        LogPrint("smartrewards-tx", "CSmartRewards::ProcessTransaction - TX %s - %.2fms\n",HexStr(tx.GetHash()), nTimeTx * 0.001);
        LogPrint("smartrewards-tx", " inputs - %.2fms\n", (nTime2 - nTime1) * 0.001);
        LogPrint("smartrewards-tx", " outputs - %.2fms\n", (nTime3 - nTime2) * 0.001);
    }

}

void CSmartRewards::CommitBlock(CBlockIndex* pIndex, const CSmartRewardsUpdateResult& result)
{
    if(!pIndex || pIndex->nHeight != currentBlock.nHeight + 1 ) throw runtime_error("CSmartRewards::ProcessTransaction - Invalid next block!");

    // Synt the data all nCacheEntires to the db.
    int preparedEntries = rewardEntries.size() + transactionEntries.size();

    if(!AddBlock(result.block, preparedEntries > nCacheEntires )){
        throw runtime_error("CSmartRewards::CommitBlock - Failed to add block entry!");
    }

    // Update the current block to the processed one
    currentBlock = result.block;

    int nTime1 = GetTimeMicros();

    // For the first round we have special parameter..
    if( !currentRound.number ){

        if( (MainNet() && pIndex->GetBlockTime() > nFirstRoundStartTime) ||
            (TestNet() && pIndex->nHeight >= nFirstRoundStartBlock_Testnet) ){

            if( !SyncPrepared() ) throw runtime_error("Failed to sync current prepared entries!");

            // Create the very first smartrewards round.
            CSmartRewardRound first;
            first.number = 1;
            first.startBlockTime = pIndex->GetBlockTime();
            first.startBlockHeight = MainNet() ? nFirstRoundStartBlock : nFirstRoundStartBlock_Testnet;
            first.endBlockTime = MainNet() ? nFirstRoundEndTime : nFirstRoundEndTime_Testnet;
            // Estimate the block, gets updated on the end of the round to the real one.
            first.endBlockHeight = MainNet() ? nFirstRoundEndBlock : nFirstRoundEndBlock_Testnet;

            CSmartRewardEntryList entries;
            CSmartRewardSnapshotList snapshots;

            // Get the current entries
            if( !GetRewardEntries(entries) ) throw runtime_error("Failed to read all reward entries!");

            // Evaluate the round and update the next rounds parameter.
            EvaluateRound(currentRound, first, entries, snapshots );

            CalculateRewardRatio(first);

            if( !StartFirstRound(first, entries) ) throw runtime_error("Failed to finalize round!");

            currentRound = first;
        }

    }else if( result.disqualifiedEntries || result.disqualifiedSmart ){

        // If there were disqualification during the last block processing
        // update the current round stats.

        currentRound.disqualifiedEntries += result.disqualifiedEntries;
        currentRound.disqualifiedSmart += result.disqualifiedSmart;

        CalculateRewardRatio(currentRound);
    }

    // If just hit the next round threshold
    if( ( MainNet() && currentRound.number < nRewardsFirstAutomatedRound - 1 && pIndex->GetBlockTime() > currentRound.endBlockTime ) ||
        ( ( TestNet() || currentRound.number >= nRewardsFirstAutomatedRound - 1 ) && pIndex->nHeight >= currentRound.endBlockHeight ) ){

        if( !SyncPrepared() ) throw runtime_error("Failed to sync current prepared entries!");

        // Write the round to the history
        currentRound.endBlockHeight = pIndex->nHeight;
        currentRound.endBlockTime = pIndex->GetBlockTime();

        CSmartRewardEntryList entries;
        CSmartRewardSnapshotList snapshots;

        // Create the next round.
        CSmartRewardRound next;
        next.number = currentRound.number + 1;
        next.startBlockTime = currentRound.endBlockTime;
        next.startBlockHeight = currentRound.endBlockHeight + 1;

        int nBlocksPerRound = GetBlocksPerRound(next.number);
        time_t startTime = (time_t)next.startBlockTime;

        if( MainNet() ){

            if( next.number == nRewardsFirstAutomatedRound - 1 ){
                // Let the round 12 end at height 574099 so that round 13 starts at 574100
                next.endBlockHeight = HF_V1_2_SMARTREWARD_HEIGHT - 1;
                next.endBlockTime = startTime + ( (next.endBlockHeight - next.startBlockHeight) * 55 );
            }else if(next.number < nRewardsFirstAutomatedRound){

                boost::gregorian::date endDate = boost::posix_time::from_time_t(startTime).date();

                endDate += boost::gregorian::months(1);
                // End date at 00:00:00 + 25200 seconds (7 hours) to match the date at 07:00 UTC
                next.endBlockTime = time_t((boost::posix_time::ptime(endDate, boost::posix_time::seconds(25200)) - epoch).total_seconds());
                next.endBlockHeight = next.startBlockHeight + ( (next.endBlockTime - next.startBlockTime) / 55 );
            }else{
                next.endBlockHeight = next.startBlockHeight + nBlocksPerRound - 1;
                next.endBlockTime = startTime + nBlocksPerRound * 55;
            }

        }else{
            next.endBlockHeight = next.startBlockHeight + nBlocksPerRound - 1;
            next.endBlockTime = startTime + nBlocksPerRound * 55;
        }

        if( !SyncPrepared() ) throw runtime_error("Failed to sync current prepared entries!");

        // Get the current entries
        if( !GetRewardEntries(entries) ) throw runtime_error("Failed to read all reward entries!");

        CalculateRewardRatio(currentRound);

        // Evaluate the round and update the next rounds parameter.
        EvaluateRound(currentRound, next, entries, snapshots);

        CalculateRewardRatio(next);

        if( !FinalizeRound(currentRound, next, entries, snapshots) ) throw runtime_error("Failed to finalize round!");

        LOCK(cs_rewardrounds);

        finishedRounds.push_back(currentRound);
        lastRound = currentRound;
        currentRound = next;
    }

    prewards->UpdateHeights(GetBlockHeight(pIndex), currentBlock.nHeight);

    int nTime2 = GetTimeMicros();

    if( LogAcceptCategory("smartrewards-block") ){
        LogPrint("smartrewards-block", "Round %d - Block: %d - Progress %d%%\n",currentRound.number, currentBlock.nHeight, int(prewards->GetProgress() * 100));
        LogPrint("smartrewards-block", "  Commit block: %.2fms\n", (nTime2 - nTime1) * 0.001);
    }
    // If we are synced notify the UI on each new block.
    // If not notify the UI every nRewardsUISyncUpdateRate blocks to let it update the
    // loading screen.
    if( IsSynced() || !(currentBlock.nHeight % nRewardsUISyncUpdateRate) )
        uiInterface.NotifySmartRewardUpdate();
}
