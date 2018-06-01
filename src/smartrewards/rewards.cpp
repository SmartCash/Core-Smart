// Copyright (c) 2018 dustinface - SmartCash Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartrewards/rewards.h"
#include "smarthive/hive.h"
#include "validation.h"
#include "init.h"
#include "ui_interface.h"
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

bool CSmartRewards::Update(CBlockIndex *pindexNew, const CChainParams& chainparams, CSmartRewardsUpdateResult &result) {

    CSmartRewardEntry *rEntry = nullptr;
    CBlock block;
    ReadBlockFromDisk(block, pindexNew, chainparams.GetConsensus());

    BOOST_FOREACH(const CTransaction &tx, block.vtx) {

        CSmartRewardTransaction testTx;
#ifdef DEBUG_LOCKORDER
        int nTime1 = GetTimeMicros();
#endif
        // First check if the transaction hash did already come up in the past.
        if( GetTransaction(tx.GetHash(),testTx)){
            // If yes we want to ignore it!
            LogPrintf("[%s] Double appearance! First in %d - Now in %d\n",testTx.hash.ToString(), testTx.blockHeight, pindexNew->nHeight);
            continue;
        }else{
            // If not save add it to the database.
            CSmartRewardTransaction saveTx(pindexNew->nHeight, tx.GetHash());
            AddTransaction(saveTx);
        }

        // No reason to check the input here for new coins.
        if( !tx.IsCoinBase() ){

            CTransaction rTx;
            uint256 rBlockHash;

            BOOST_FOREACH(const CTxIn &in, tx.vin) {

                if( in.scriptSig.IsZerocoinSpend() ) continue;

                if(!::GetTransaction(in.prevout.hash,rTx,chainparams.GetConsensus(),rBlockHash)){
                    return error("%s: GetTransaction - %s\n Input: %s", __func__, tx.ToString(),in.prevout.hash.ToString());
                }

                CTxOut rOut = rTx.vout[in.prevout.n];

                std::vector<CSmartAddress> ids;

                int required = ParseScript(rOut.scriptPubKey ,ids);

                if( !required || required > 1 || ids.size() > 1 ){
                    return error("Could't parse CSmartAddress: %s",rOut.ToString());
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

                if( rEntry->eligible ){
                    rEntry->eligible = false;
                    result.disqualifiedEntries++;
                    result.disqualifiedSmart += rEntry->balanceOnStart;
                }

                if(rEntry->balance < 0 ){
                    LogPrintf("%s: Negative amount?! - %s", __func__, rEntry->ToString());
                    rEntry->balance = 0;
                 }

            }
        }
#ifdef DEBUG_LOCKORDER
        int nTime2 = GetTimeMicros();
#endif

        BOOST_FOREACH(const CTxOut &out, tx.vout) {

            if(out.scriptPubKey.IsZerocoinMint() ) continue;

            std::vector<CSmartAddress> ids;
            int required = ParseScript(out.scriptPubKey ,ids);

            if( !required || required > 1 || ids.size() > 1 ){
                return error("Could't parse CSmartAddress: %s",out.ToString());
            }else{
                if(!GetCachedRewardEntry(ids.at(0),rEntry)){
                    rEntry = new CSmartRewardEntry(ids.at(0));
                    ReadRewardEntry(rEntry->id, *rEntry);
                    rewardEntries.insert(make_pair(ids.at(0), rEntry));
                }
                rEntry->balance += out.nValue;
            }
        }

#ifdef DEBUG_LOCKORDER
        int nTime3 = GetTimeMicros();
        int nTimeTx = nTime3 - nTime1;

        if( nTimeTx > 500000){
            LogPrint("smartrewards", "CSmartRewards::Update TX %s - %.2fms\n",HexStr(tx.GetHash()), nTimeTx * 0.001);
            LogPrint("smartrewards", " inputs - %.2fms\n", (nTime2 - nTime1) * 0.001);
            LogPrint("smartrewards", " outputs - %.2fms\n", (nTime3 - nTime2) * 0.001);
        }
#endif

    }

    uint256 blockHash = block.GetHash();
    result.block = CSmartRewardBlock(pindexNew->nHeight, blockHash, block.GetBlockTime());

    // Synt the data all nCacheEntires to the db.
    int preparedEntries = rewardEntries.size() + transactionEntries.size();

    return AddBlock(result.block, preparedEntries > nCacheEntires );
}

void CSmartRewards::EvaluateRound(CSmartRewardRound &current, CSmartRewardRound &next, CSmartRewardEntryList &entries, CSmartRewardSnapshotList &snapshots)
{
    LOCK(cs_rewardsdb);
    snapshots.clear();

    BOOST_FOREACH(CSmartRewardEntry &entry, entries) {

        if( current.number ) snapshots.push_back(CSmartRewardSnapshot(entry, current));

        entry.balanceOnStart = entry.balance;
        entry.eligible = entry.balanceOnStart >= SMART_REWARDS_MIN_BALANCE && !SmartHive::IsHive(entry.id);

        if( entry.eligible ){
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


// --- TBD ---
bool CSmartRewards::RestoreSnapshot(const int16_t round)
{
//    LOCK(cs_rewardsdb);

//    CSmartRewardRound restore;
//    CSmartRewardEntryList entries;

//    if( !pdb->ReadRound(round, restore) ) return false;
//    if( !pdb->ReadRewardSnapshots(round, entries) ) return false;
//    if( history.number != round) return false;

//    restore.disqualifiedEntries = 0;
//    restore.disqualifiedSmart = 0;
//    restore.rewards = 0;
//    restore.percent = 0;

//    CalculateRewardRatio(restore);

//    return pdb->ResetToRound(round, restore, entries);
    return false;
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

    // Destructor of the elements is called in the pdb->SyncBlocks.
    rewardEntries.clear();
    blockEntries.clear();
    transactionEntries.clear();

    return ret;
}

bool CSmartRewards::IsSynced()
{
    return (chainHeight - rewardHeight) <= nRewardsSyncDistance - 1;
}

double CSmartRewards::GetProgress()
{
    double progress = chainHeight > nRewardsSyncDistance ? double(rewardHeight) / double(chainHeight - nRewardsSyncDistance) : 0.0;
    return progress > 1 ? 1 : progress;
}

int CSmartRewards::GetLastHeight()
{
    return rewardHeight;
}

bool CSmartRewards::AddBlock(const CSmartRewardBlock &block, bool sync)
{
    blockEntries.push_back(block);

#ifdef DEBUG_LOCKORDER
    int nTime1 = GetTimeMicros();
#endif

    if(sync) return SyncPrepared();

#ifdef DEBUG_LOCKORDER
    int nTimeSync = GetTimeMicros() - nTime1;
    if( nTimeSync > 300000){
        LogPrint("smartrewards", "CSmartRewards::AddBlock - Sync - Block: %d - Progress %.2fms\n",block.nHeight, nTimeSync * 0.001);
    }
#endif

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

void CSmartRewards::CatchUp()
{
    const CChainParams& chainparams = Params();
    CBlockIndex* pHighestIndex = chainActive.Tip();
    CBlockIndex* pLastIndex = pHighestIndex;
    if( !pLastIndex ) return;
    // If the rewards db is higher than the chain.
    if( pLastIndex->nHeight <= currentBlock.nHeight ) return;

    // Search the index of the next missing bock in the
    // rewards database.
    while( pLastIndex->nHeight != currentBlock.nHeight + 1){
        pLastIndex = pLastIndex->pprev;
    }

    while( pLastIndex && ( currentBlock.nHeight - pLastIndex->nHeight ) < nRewardsConfirmations)
    {
        if( ShutdownRequested() ){
            SyncPrepared();
            return;
        }

        if( !(currentBlock.nHeight % 100) ){
            uiInterface.InitMessage(_("Creating SmartRewards database: ") + strprintf("%d/%d",currentBlock.nHeight, pHighestIndex->nHeight));
        }

        ProcessBlock(pLastIndex, chainparams);

        pLastIndex = chainActive.Next(pLastIndex);

    }

    prewards->UpdateHeights(GetBlockHeight(pHighestIndex), currentBlock.nHeight);
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

void CSmartRewards::ProcessBlock(CBlockIndex* pLastIndex, const CChainParams& chainparams)
{
    int64_t nTime1 = 0, nTime2 = 0, nTime3 = 0;
    static int64_t nTimeTotal = 0;
    static int64_t nTimeUpdateRewardsTotal = 0;
    static int64_t nCountUpdateRewards = 0;

    if( ( pLastIndex->nHeight - currentBlock.nHeight ) > nRewardsConfirmations){

        CBlockIndex* pNextIndex = pLastIndex;

        while(pNextIndex && pNextIndex->nHeight != currentBlock.nHeight + 1 ) pNextIndex = pNextIndex->pprev;

        if(!pNextIndex || pNextIndex->nHeight != currentBlock.nHeight + 1 ) throw runtime_error("Could't find next block index!");

        nTime1 = GetTimeMicros();

        // Result of the block processing.
        CSmartRewardsUpdateResult result;
        // Process the block!
        if(!Update(pNextIndex, chainparams, result)) throw runtime_error(std::string(__func__) + ": rewards update failed");

        // Update the current block to the processed one
        currentBlock = result.block;

        nTime2 = GetTimeMicros();

        nTimeUpdateRewardsTotal += nTime2 - nTime1;
        ++nCountUpdateRewards;

        // For the first round we have special parameter..
        if( !currentRound.number ){

            if( (MainNet() && pNextIndex->GetBlockTime() > nFirstRoundStartTime) ||
                (TestNet() && pNextIndex->nHeight >= nFirstRoundStartBlock_Testnet) ){

                if( !SyncPrepared() ) throw runtime_error("Could't sync current prepared entries!");

                // Create the very first smartrewards round.
                CSmartRewardRound first;
                first.number = 1;
                first.startBlockTime = pNextIndex->GetBlockTime();
                first.startBlockHeight = MainNet() ? nFirstRoundStartBlock : nFirstRoundStartBlock_Testnet;
                first.endBlockTime = MainNet() ? nFirstRoundEndTime : nFirstRoundEndTime_Testnet;
                // Estimate the block, gets updated on the end of the round to the real one.
                first.endBlockHeight = MainNet() ? nFirstRoundEndBlock : nFirstRoundEndBlock_Testnet;

                CSmartRewardEntryList entries;
                CSmartRewardSnapshotList snapshots;

                // Get the current entries
                if( !GetRewardEntries(entries) ) throw runtime_error("Could't read all reward entries!");

                // Evaluate the round and update the next rounds parameter.
                EvaluateRound(currentRound, first, entries, snapshots );

                CalculateRewardRatio(first);

                if( !StartFirstRound(first, entries) ) throw runtime_error("Could't finalize round!");

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
        if( ( MainNet() && currentRound.number < nRewardsFirstAutomatedRound && pNextIndex->GetBlockTime() > currentRound.endBlockTime ) ||
            ( ( TestNet() || currentRound.number >= nRewardsFirstAutomatedRound ) && pNextIndex->nHeight >= currentRound.endBlockHeight ) ){

            if( !SyncPrepared() ) throw runtime_error("Could't sync current prepared entries!");

            // Write the round to the history
            currentRound.endBlockHeight = pNextIndex->nHeight;
            currentRound.endBlockTime = pNextIndex->GetBlockTime();

            CSmartRewardEntryList entries;
            CSmartRewardSnapshotList snapshots;

            // Create the next round.
            CSmartRewardRound next;
            next.number = currentRound.number + 1;
            next.startBlockTime = currentRound.endBlockTime;
            next.startBlockHeight = currentRound.endBlockHeight + 1;

            time_t startTime = (time_t)next.startBlockTime;

            boost::gregorian::date endDate = boost::posix_time::from_time_t(startTime).date();

            endDate += boost::gregorian::months(1);
            // End date at 00:00:00 + 25200 seconds (7 hours) to match the date at 07:00 UTC
            next.endBlockTime = time_t((boost::posix_time::ptime(endDate, boost::posix_time::seconds(25200)) - epoch).total_seconds());

            if( TestNet() ) next.endBlockTime = startTime + nRewardsBlocksPerRound_Testnet * 55;

            // Estimate the block, gets updated on the end of the round to the real one.
            if( MainNet() )  next.endBlockHeight = next.startBlockHeight + nRewardsBlocksPerRound - 1;
            else             next.endBlockHeight = next.startBlockHeight + nRewardsBlocksPerRound_Testnet - 1;

            if( !SyncPrepared() ) throw runtime_error("Could't sync current prepared entries!");

            // Get the current entries
            if( !GetRewardEntries(entries) ) throw runtime_error("Could not read all reward entries!");

            CalculateRewardRatio(currentRound);

            // Evaluate the round and update the next rounds parameter.
            EvaluateRound(currentRound, next, entries, snapshots);

            CalculateRewardRatio(next);

            if( !FinalizeRound(currentRound, next, entries, snapshots) ) throw runtime_error("Could't finalize round!");

            LOCK(cs_rewardrounds);

            finishedRounds.push_back(currentRound);
            lastRound = currentRound;
            currentRound = next;
        }

        prewards->UpdateHeights(GetBlockHeight(pLastIndex), currentBlock.nHeight);

        nTime3 = GetTimeMicros(); nTimeTotal += nTime3 - nTime1;
        int nTimeUpdateMean = nTimeUpdateRewardsTotal/nCountUpdateRewards;
        int nTimeUpdate = nTime2 - nTime1;

        if( nTimeUpdate > nTimeUpdateMean * 100){
            LogPrint("smartrewards", "Round %d - Block: %d - Progress %d%%\n",currentRound.number, currentBlock.nHeight, int(prewards->GetProgress() * 100));
            LogPrint("smartrewards", "  Update rewards: %.2fms [%.2fms]\n", nTimeUpdate * 0.001, (nTimeUpdateMean) * 0.001);
            LogPrint("smartrewards", "  Evaluate round: %.2fms\n", (nTime3 - nTime2) * 0.001);
            LogPrint("smartrewards", "  Total: %.2fms [%.2fs]\n", (nTime3 - nTime1) * 0.001, nTimeTotal * 0.000001);
        }
        // If we are synced notify the UI on each new block.
        // If not notify the UI every nRewardsUISyncUpdateRate blocks to let it update the
        // loading screen.
        if( IsSynced() || !(currentBlock.nHeight % nRewardsUISyncUpdateRate) )
            uiInterface.NotifySmartRewardUpdate();
    }
}

