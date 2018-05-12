// Copyright (c) 2018 dustinface - SmartCash Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartrewards/rewards.h"
#include "validation.h"
#include "init.h"
#include "ui_interface.h"
#include <boost/thread.hpp>
#include <boost/range/irange.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>

CSmartRewards *prewards = NULL;

// Exclude the following addresses form SmartRewards.
static std::vector<CSmartRewardId> blacklist = {
    CSmartRewardId("SXun9XDHLdBhG4Yd1ueZfLfRpC9kZgwT1b"), // Community treasure
    CSmartRewardId("SW2FbVaBhU1Www855V37auQzGQd8fuLR9x"), // Support hive
    CSmartRewardId("SPusYr5tUdUyRXevJg7pnCc9Sm4HEzaYZF"), // Development hive
    CSmartRewardId("Siim7T5zMH3he8xxtQzhmHs4CQSuMrCV1M"), // Outreach hive
    CSmartRewardId("SU5bKb35xUV8aHG5dNarWHB3HBVjcCRjYo"), // Legacy smartrewards
    CSmartRewardId("SNxFyszmGEAa2n2kQbzw7gguHa5a4FC7Ay"), // New hive 1
    CSmartRewardId("Sgq5c4Rznibagv1aopAfPA81jac392scvm"), // New hive 2
    CSmartRewardId("Sc61Gc2wivtuGd6recqVDqv4R38TcHqFS8") // New hive 3
};

int ParseScript(const CScript &script, std::vector<CSmartRewardId> &ids){

    std::vector<CTxDestination> addresses;
    txnouttype type;
    int nRequired;

    if (!ExtractDestinations(script, type, addresses, nRequired)) {
        return 0;
    }

    BOOST_FOREACH(const CTxDestination &d, addresses)
    {
        ids.push_back(CSmartRewardId(d));
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
    LOCK(csDb);
    return pdb->Verify();
}

bool CSmartRewards::Update(CBlockIndex *pindexNew, const CChainParams& chainparams, CSmartRewardsUpdateResult &result, bool sync) {

    if(fLiteMode) return true; // disable SmartRewards sync in litemode

    LOCK(csDb);

    CBlock block;
    ReadBlockFromDisk(block, pindexNew, chainparams.GetConsensus());

    BOOST_FOREACH(const CTransaction &tx, block.vtx) {

        CSmartRewardTransaction testTx;

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

                bool added;
                std::vector<CSmartRewardId> ids;
                CSmartRewardEntry rEntry;
                int required = ParseScript(rOut.scriptPubKey ,ids);

                if( !required || required > 1 || ids.size() > 1 ){
                    return error("Could't parse CSmartRewardId: %s",rOut.ToString());
                }

                GetRewardEntry(ids.at(0),rEntry, added);

                if(added){
                    LogPrintf("%s: Spend without previous receive - %s", __func__, tx.ToString());
                    continue;
                }

                rEntry.balance -= rOut.nValue;

                if( rEntry.eligible ){
                    rEntry.eligible = false;
                    result.disqualifiedEntries++;
                    result.disqualifiedSmart += rEntry.balanceOnStart;
                }

                if(rEntry.balance < 0 ){
                    LogPrintf("%s: Negative amount?! - %s", __func__, rEntry.ToString());
                    rEntry.balance = 0;
                 }

                if( rEntry.balance == 0 ){
                    PrepareForRemove(rEntry);
                }else{
                    PrepareForUpdate(rEntry);
                }

            }
        }

        BOOST_FOREACH(const CTxOut &out, tx.vout) {

            if(out.scriptPubKey.IsZerocoinMint() ) continue;

            bool added;
            std::vector<CSmartRewardId> ids;
            CSmartRewardEntry rEntry;
            int required = ParseScript(out.scriptPubKey ,ids);

            if( !required || required > 1 || ids.size() > 1 ){
                return error("Could't parse CSmartRewardId: %s",out.ToString());
            }else{
                GetRewardEntry(ids.at(0),rEntry, added);

                rEntry.balance += out.nValue;

                PrepareForUpdate(rEntry);
            }
        }
    }

    uint256 blockHash = block.GetHash();
    result.block = CSmartRewardBlock(pindexNew->nHeight, blockHash, block.GetBlockTime());

    return AddBlock(result.block, sync);
}

void CSmartRewards::EvaluateRound(CSmartRewardRound &current, CSmartRewardRound &next, CSmartRewardEntryList &entries, CSmartRewardSnapshotList &snapshots)
{
    LOCK(csDb);
    snapshots.clear();

    BOOST_FOREACH(CSmartRewardEntry &entry, entries) {

        if( current.number ) snapshots.push_back(CSmartRewardSnapshot(entry, current));

        auto blacklisted = std::find(blacklist.begin(), blacklist.end(), entry.id);

        entry.balanceOnStart = entry.balance;
        entry.eligible = entry.balanceOnStart >= SMART_REWARDS_MIN_BALANCE && blacklisted == blacklist.end();

        if( entry.eligible ){
            ++next.eligibleEntries;
            next.eligibleSmart += entry.balanceOnStart;
        }
    }
}

bool CSmartRewards::StartFirstRound(const CSmartRewardRound &first, const CSmartRewardEntryList &entries)
{
    LOCK(csDb);
    return pdb->StartFirstRound(first,entries);
}

bool CSmartRewards::FinalizeRound(const CSmartRewardRound &current, const CSmartRewardRound &next, const CSmartRewardEntryList &entries, const CSmartRewardSnapshotList &snapshots)
{
    LOCK(csDb);
    return pdb->FinalizeRound(current, next, entries, snapshots);
}

bool CSmartRewards::GetRewardSnapshots(const int16_t round, CSmartRewardSnapshotList &snapshots)
{
    LOCK(csDb);
    return pdb->ReadRewardSnapshots(round, snapshots);
}

bool CSmartRewards::GetRewardPayouts(const int16_t round, CSmartRewardSnapshotList &payouts)
{
    LOCK(csDb);
    return pdb->ReadRewardPayouts(round, payouts);
}


// --- TBD ---
bool CSmartRewards::RestoreSnapshot(const int16_t round)
{
//    LOCK(csDb);

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

void CSmartRewards::RemovePrepared(const CSmartRewardEntry &entry)
{
    // Remove the entries if its marked to become removed.
    auto checkRemove = std::find(removeEntries.begin(), removeEntries.end(), entry);
    if( checkRemove != removeEntries.end() ) removeEntries.erase(checkRemove);
    // Remove the entries if its marked to become updated.
    auto checkUpdate = std::find(updateEntries.begin(), updateEntries.end(), entry);
    if( checkUpdate != updateEntries.end() ) updateEntries.erase(checkUpdate);
}

void CSmartRewards::PrepareForUpdate(const CSmartRewardEntry &entry)
{
    LOCK(csDb);

    // If it is prepared for update/remove delete it first.
    RemovePrepared(entry);

    updateEntries.push_back(entry);
}

void CSmartRewards::PrepareForRemove(const CSmartRewardEntry &entry)
{
    LOCK(csDb);

    // If it is prepared for update/remove delete it first.
    RemovePrepared(entry);

    // And add the new one.
    removeEntries.push_back(entry);
}

bool CSmartRewards::GetRewardEntry(const CSmartRewardId &id, CSmartRewardEntry &entry)
{
    bool added;
    GetRewardEntry(id, entry, added);
    return !added;
}

void CSmartRewards::GetRewardEntry(const CSmartRewardId &id, CSmartRewardEntry &entry, bool &added)
{
    LOCK(csDb);

    added = false;

    auto checkEntry = [id](const CSmartRewardEntry &e) -> bool {
                                return e.id == id;
                           };

    // Return the entry prepared for update if it exists.
    auto checkRemove = std::find_if(removeEntries.begin(), removeEntries.end(), checkEntry);

    if( checkRemove != removeEntries.end() ){
        entry = *checkRemove;
        return;
    }

    // Return the entry prepared for remove if it exists.
    auto checkUpdate = std::find_if(updateEntries.begin(), updateEntries.end(), checkEntry);

    if( checkUpdate != updateEntries.end() ){
        entry = *checkUpdate;
        return;
    }

    // Otherwise use the one from the database.
    if( !pdb->ReadRewardEntry(id, entry)){

        //If it was not yet in the db create it
        entry.id = id;
        entry.balanceOnStart = 0;
        entry.balance = 0;
        entry.eligible = false;

        // and mark it as added!
        added = true;
    }

}

bool CSmartRewards::GetRewardEntries(CSmartRewardEntryList &entries)
{
    LOCK(csDb);
    return pdb->ReadRewardEntries(entries);
}

bool CSmartRewards::SyncPrepared()
{
    LOCK(csDb);

    bool ret =  pdb->SyncBlocks(blockEntries,updateEntries,removeEntries, transactionEntries);

    updateEntries.clear();
    removeEntries.clear();
    blockEntries.clear();
    transactionEntries.clear();

    return ret;
}

bool CSmartRewards::IsSynced()
{
    return (chainHeight - rewardHeight) <= nRewardsSyncDistance;
}

double CSmartRewards::GetProgress()
{
    double progress = chainHeight > nRewardsSyncDistance ? double(rewardHeight) / double(chainHeight - nRewardsSyncDistance) : 0.0;
    return progress > 1 ? 1 : progress;
}

bool CSmartRewards::AddBlock(const CSmartRewardBlock &block, bool sync)
{
    LOCK(csDb);

    blockEntries.push_back(block);

    if(sync) return SyncPrepared();

    return true;
}

void CSmartRewards::AddTransaction(const CSmartRewardTransaction &transaction)
{
    LOCK(csDb);
    transactionEntries.push_back(transaction);
}

bool CSmartRewards::GetLastBlock(CSmartRewardBlock &block)
{
    LOCK(csDb);
    // Read the last block stored in the rewards database.
    return pdb->ReadLastBlock(block);
}

bool CSmartRewards::GetTransaction(const uint256 hash, CSmartRewardTransaction &transaction)
{
    LOCK(csDb);

    // If the transaction is already in the cache use this one.
    BOOST_FOREACH(CSmartRewardTransaction t, transactionEntries) {
        if(t.hash == hash){
            transaction = t;
            return true;
        }
    }

    return pdb->ReadTransaction(hash, transaction);
}

bool CSmartRewards::GetCurrentRound(CSmartRewardRound &round)
{
    LOCK(csDb);
    return pdb->ReadCurrentRound(round);
}

bool CSmartRewards::GetRewardRounds(CSmartRewardRoundList &vect)
{
    LOCK(csDb);
    return pdb->ReadRounds(vect);
}

void CSmartRewards::UpdateHeights(const int nHeight, const int nRewardHeight)
{
    chainHeight = nHeight;
    rewardHeight = nRewardHeight;
}


bool CSmartRewards::UpdateCurrentRound(const CSmartRewardRound &round)
{
    LOCK(csDb);
    return pdb->WriteCurrentRound(round);
}

void ThreadSmartRewards()
{

    if(fLiteMode) return; // disable SmartRewards sync in litemode

    static bool fOneThread;
    if(fOneThread) return;
    fOneThread = true;

    // Make this thread recognisable as the SmartRewards thread
    RenameThread("smartrewards");

    // Used for time conversions.
    boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));

    // Estimate or return the current block height.
    std::function<int (const CBlockIndex*)> getBlockHeight = [](const CBlockIndex *index) {
        int64_t syncDiff = std::time(0) - index->GetBlockTime();
        int64_t genesisDiff = std::time(0) - nFirstTxTimestamp; // Diff from the first transaction till now.
        return syncDiff > 1200 ? genesisDiff / 55 : index->nHeight;
    };

    CSmartRewardBlock currentBlock;

    // Get the last written block of the rewards database.
    if(!prewards->GetLastBlock(currentBlock)){
        // If there is no one available yet
        // Use 0 to get 1 as start below.
        currentBlock.nHeight = 0;
        currentBlock.blockHash = uint256();
        currentBlock.blockTime = 0;
    }

    // Used as last used index of the sync process.
    CBlockIndex *lastIndex = NULL;
    // Used as next index of the sync process.
    CBlockIndex *nextIndex = NULL;
    // Used for the highest block index
    CBlockIndex *currentIndex = NULL;
    CChainParams chainparams = Params();

    // Used to determine the time of the next SmartRewards UI update.
    int64_t lastUIUpdate = 0;

    int64_t nTimeTotal = 0;
    int64_t nTimeUpdateRewardsTotal = 0;
    int64_t nCountUpdateRewards = 0;

    // Initialize the UI
    {
        LOCK(cs_main);
        currentIndex = chainActive.Tip();
    }

    int chainHeight = getBlockHeight(currentIndex);
    prewards->UpdateHeights(chainHeight, currentBlock.nHeight);
    uiInterface.NotifySmartRewardUpdate();

    while (true)
    {
        int64_t nTime1 = GetTimeMicros();

        {
            LOCK(cs_main);

            // Allow the shutdown interrupt to crash into.
            boost::this_thread::interruption_point();
            // If the user want to close the wallet, step out.
            if(ShutdownRequested()) return;

            // Get index of the last block in the chain.
            currentIndex = chainActive.Tip();
        }

        int64_t nTime2 = GetTimeMicros();

        // If there is no block available. Should not happen!
        if(!currentIndex){
            lastIndex = NULL;
            MilliSleep(1000);
            continue;
        }

        if(!lastIndex){
            // First run or on error below we use the current block height to find the
            // start point of the sync.
            lastIndex = currentIndex;

            // If the rewards database is older then the chaindata
            // break it. Happens if the chain gets deleted
            // and the rewardsdb not.
            if( currentBlock.nHeight + 1 > lastIndex->nHeight ){
                lastIndex = NULL;
                MilliSleep(1000);
                continue;
            }

            // Search the index of the next missing bock in the
            // rewards database.
            while( lastIndex->nHeight != currentBlock.nHeight + 1){
                lastIndex = lastIndex->pprev;
            }

            // Use it as next.
            nextIndex = lastIndex;

        }else{
            LOCK(cs_main);
            nextIndex = chainActive.Next(lastIndex);
        }

        // If there is no next index available yet or the next detected has less
        // than 10 confirmations, wait a bit..
        if( !nextIndex || (currentIndex->nHeight - nextIndex->nHeight) < 10 ){
            lastIndex = NULL;
            MilliSleep(1000);
            continue;
        }

        int64_t nTime3 = GetTimeMicros();

        // Result of the block processing.
        CSmartRewardsUpdateResult result;
        // Synt the data all nCacheBlocks to the db.
        bool sync = currentBlock.nHeight > 0 && !(currentBlock.nHeight % nCacheBlocks);
        // Process the block!
        if(!prewards->Update(nextIndex, chainparams, result, sync)) throw runtime_error(std::string(__func__) + ": rewards update failed");

        // Update the current block to the processed one
        currentBlock = result.block;
        // Next round we use the current of this round as last.
        lastIndex = nextIndex;

        int64_t nTime4 = GetTimeMicros();

        nTimeUpdateRewardsTotal += nTime4 - nTime3;
        ++nCountUpdateRewards;

        CSmartRewardRound round;
        bool snapshot = false;

        if( !prewards->GetCurrentRound(round) ){

            if( lastIndex->GetBlockTime() > firstRoundStartTime){

                snapshot = true;

                if( !prewards->SyncPrepared() ) throw runtime_error("Could't sync current prepared entries!");

                // Create the very first smartrewards round.
                CSmartRewardRound first;
                first.number = 1;
                first.startBlockTime = lastIndex->GetBlockTime();
                first.startBlockHeight = firstRoundStartBlock;
                first.endBlockTime = firstRoundEndTime;
                // Estimate the block, gets updated on the end of the round to the real one.
                first.endBlockHeight = firstRoundEndBlock;

                CSmartRewardEntryList entries;
                CSmartRewardSnapshotList snapshots;

                // Get the current entries
                if( !prewards->GetRewardEntries(entries) ) throw runtime_error("Could't read all reward entries!");

                // Evaluate the round and update the next rounds parameter.
                prewards->EvaluateRound(round, first, entries, snapshots );

                CalculateRewardRatio(first);

                if( !prewards->StartFirstRound(first, entries) ) throw runtime_error("Could't finalize round!");
            }

        }else if( result.disqualifiedEntries || result.disqualifiedSmart ){

            // If there were disqualification during the last block processing
            // update the current round stats.

            round.disqualifiedEntries += result.disqualifiedEntries;
            round.disqualifiedSmart += result.disqualifiedSmart;

            CalculateRewardRatio(round);

            if( !prewards->UpdateCurrentRound(round) ) throw runtime_error("Could't update current round!");
        }

        // If just hit the next round threshold
        if(round.number && lastIndex->GetBlockTime() > round.endBlockTime){

            snapshot = true;

            if( !prewards->SyncPrepared() ) throw runtime_error("Could't sync current prepared entries!");

            // Write the round to the history
            round.endBlockHeight = lastIndex->nHeight;
            round.endBlockTime = lastIndex->GetBlockTime();

            CSmartRewardEntryList entries;
            CSmartRewardSnapshotList snapshots;

            // Create the next round.
            CSmartRewardRound next;
            next.number = round.number + 1;
            next.startBlockTime = round.endBlockTime;
            next.startBlockHeight = round.endBlockHeight + 1;

            time_t startTime = (time_t)next.startBlockTime;

            boost::gregorian::date endDate = boost::posix_time::from_time_t(startTime).date()
                                               + boost::gregorian::months(1);

            // End date at 00:00:00 + 25200 seconds (7 hours) to match the date at 07:00 UTC
            next.endBlockTime = time_t((boost::posix_time::ptime(endDate, boost::posix_time::seconds(25200)) - epoch).total_seconds());

            // Estimate the block, gets updated on the end of the round to the real one.
            next.endBlockHeight = next.startBlockHeight + (next.endBlockTime - next.startBlockTime) / 55;

            if( !prewards->SyncPrepared() ) throw runtime_error("Could't sync current prepared entries!");

            // Get the current entries
            if( !prewards->GetRewardEntries(entries) ) throw runtime_error("Could not read all reward entries!");

            CalculateRewardRatio(round);

            // Evaluate the round and update the next rounds parameter.
            prewards->EvaluateRound(round, next, entries, snapshots);

            CalculateRewardRatio(next);

            if( !prewards->FinalizeRound(round, next, entries, snapshots) ) throw runtime_error("Could't finalize round!");
        }

        prewards->UpdateHeights(getBlockHeight(currentIndex), lastIndex->nHeight);

        int64_t nTime5 = GetTimeMicros();

        // If we are synced notify the UI on each new block.
        // If not notify the UI every nRewardsUISyncUpdateRate blocks to let it update the
        // loading screen.
        if( prewards->IsSynced() || !lastUIUpdate || lastIndex->nHeight - lastUIUpdate > nRewardsUISyncUpdateRate ){
            lastUIUpdate = lastIndex->nHeight;
            uiInterface.NotifySmartRewardUpdate();
        }

        int64_t nTime6 = GetTimeMicros(); nTimeTotal += nTime6 - nTime1;

        if(!(currentBlock.nHeight % 10000) || snapshot){
            LogPrintf("Round %d - Block: %d - Progress %d%%\n",round.number, currentBlock.nHeight, int(prewards->GetProgress() * 100));
            LogPrintf("  Get index : %.2fms\n", (nTime2 - nTime1) * 0.001);
            LogPrintf("  Next index: %.2fms\n", (nTime3 - nTime2) * 0.001);
            LogPrintf("  Update rewards: %.2fms [%.2fms]\n", (nTime4 - nTime3) * 0.001, (nTimeUpdateRewardsTotal/nCountUpdateRewards) * 0.001);
            LogPrintf("  Evaluate round: %.2fms\n", (nTime5 - nTime4) * 0.001);
            LogPrintf("  Notify UI: %.2fms\n", (nTime6 - nTime5) * 0.001);
            LogPrintf("  Total: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
        }
    }
}

