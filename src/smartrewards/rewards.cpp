#include "smartrewards/rewards.h"
#include "validation.h"
#include "init.h"

CSmartRewards *prewards = NULL;


bool CSmartRewards::Verify()
{
    LOCK(csDb);
    return pdb->Verify();
}

bool CSmartRewards::Update(CBlockIndex *pindexNew, const CChainParams& chainparams, CSmartRewardsUpdateResult &result, bool sync) {

    if(fLiteMode) return true; // disable SmartRewards sync in litemode

    LOCK(csDb);

    bool ret = true;

    CBlock block;
    ReadBlockFromDisk(block, pindexNew, chainparams.GetConsensus());

    CTransaction rTx;
    uint256 rBlockHash;

    BOOST_FOREACH(const CTransaction tx, block.vtx) {

         BOOST_FOREACH(const CTxOut out, tx.vout) {

            if(out.scriptPubKey.IsZerocoinMint()) continue;

            bool added;
            CSmartRewardEntry rEntry;
            GetRewardEntry(out.scriptPubKey,rEntry, added);

            rEntry.balance += out.nValue;

            MarkForUpdate(rEntry);
         }

         // No reason to check the input here
         if( tx.IsCoinBase()) continue;

         BOOST_FOREACH(const CTxIn in, tx.vin) {

             if(in.scriptSig.IsZerocoinSpend()) continue;

             if(!GetTransaction(in.prevout.hash,rTx,chainparams.GetConsensus(),rBlockHash)){
                   return error("%s: GetTransaction - %s", __func__, tx.ToString());
             }

             CTxOut rOut = rTx.vout[in.prevout.n];

             bool added;
             CSmartRewardEntry rEntry;

             GetRewardEntry(rOut.scriptPubKey, rEntry, added);

             if(added)
                 return error("%s: Spend without previous receive - %s", __func__, tx.ToString());

             rEntry.balance -= rOut.nValue;

             if(rEntry.eligible ){
                 rEntry.eligible = false;
                 result.disqualifiedEntries++;
                 result.disqualifiedSmart -= rEntry.balanceOnStart;
             }

             if( rEntry.balance == 0 ){
                 // Remove
                 MarkForRemove(rEntry);
             }else if(rEntry.balance < 0 ){
                 return error("%s: Negative amount?! - %s", __func__, rEntry.ToString());
             }else{
                 MarkForUpdate(rEntry);
             }

         }

    }

    uint256 blockHash = block.GetHash();
    result.block = CSmartRewardsBlock(pindexNew->nHeight, blockHash, block.GetBlockTime());

    ret = SyncMarkups(result.block, sync);

    return ret;
}

bool CSmartRewards::EvaluateCurrentRound(CSmartRewardsRound &next)
{
    LOCK(csDb);

    CSmartRewardsRound current;
    bool firstRound = !pdb->ReadCurrentRound(current);

    std::vector<CSmartRewardEntry> entries;
    std::vector<CSmartRewardPayout> payouts;

    if( !pdb->ReadRewardEntries(entries) ) throw runtime_error("Could not read all reward entries!");

    BOOST_FOREACH(CSmartRewardEntry &e, entries) {

        if( e.eligible && !firstRound ) payouts.push_back(CSmartRewardPayout(e, current));

        e.balanceLastStart = e.balanceOnStart;
        e.balanceOnStart = e.balance;

        if( e.balanceOnStart >= SMART_REWARDS_MIN_BALANCE ){

            e.eligible = true;

            ++next.eligibleEntries;
            next.eligibleSmart += e.balanceOnStart;
        }else{
            e.eligible = false;
        }

    }

    if( !pdb->WriteRewardEntries(entries) ) throw runtime_error("Could not write all reward entries!");

    if( !pdb->WriteRewardPayouts(current.number, payouts) ) throw runtime_error("Could not write all reward entries!");

    return true;
}

void CSmartRewards::MarkForUpdate(CSmartRewardEntry entry)
{

    for (auto i = removeEntries.begin(); i != removeEntries.end(); ) {

      if (*i == entry) {
        i = removeEntries.erase(i);
        break;
      } else {
        ++i;
      }

    }

    for (auto i = updateEntries.begin(); i != updateEntries.end(); ) {

      if (*i == entry) {
        i = updateEntries.erase(i);
        break;
      } else {
        ++i;
      }

    }

    updateEntries.push_back(entry);
}

void CSmartRewards::MarkForRemove(CSmartRewardEntry entry)
{
    // If the entry is already marked for to become removed
    // return remove it from the list.
    for (auto i = removeEntries.begin(); i != removeEntries.end(); ) {

      if (*i == entry) {
        i = removeEntries.erase(i);
        break;
      } else {
        ++i;
      }

    }
    // If the entry is already marked for to become updated
    // return remove it from the list.
    for (auto i = updateEntries.begin(); i != updateEntries.end(); ) {

      if (*i == entry) {
        i = updateEntries.erase(i);
        break;
      } else {
        ++i;
      }

    }

    // And add the new one.
    removeEntries.push_back(entry);
}

void CSmartRewards::GetRewardEntry(const CScript &pubKey, CSmartRewardEntry &entry, bool &added)
{
    added = false;

    // If the entry is already marked for to become updated
    // return this one.
    BOOST_FOREACH(CSmartRewardEntry e, updateEntries) {
        if(e.pubKey == pubKey){
            entry = e;
            return;
        }
    }

    // If the entry is already marked for to become removed
    // return this one.
    BOOST_FOREACH(CSmartRewardEntry e, removeEntries) {
        if(e.pubKey == pubKey){
            entry = e;
            return;
        }
    }

    // Otherwise use the one from the database.
    if( !pdb->ReadRewardEntry(pubKey, entry)){

        //If it was not yet in the db create it
        entry.pubKey = pubKey;
        entry.balanceLastStart = 0;
        entry.balanceOnStart = 0;
        entry.balance = 0;

        // and mark it as added!
        added = true;
    }

}

void CSmartRewards::ResetMarkups()
{
    updateEntries.clear();
    removeEntries.clear();
    blockEntries.clear();
}


bool CSmartRewards::SyncMarkups(const CSmartRewardsBlock &block, bool sync)
{
    LOCK(csDb);

    bool ret = true;

    blockEntries.push_back(block);

    if(sync){
        // Write to the database.
        ret = pdb->Sync(blockEntries,updateEntries,removeEntries);
        ResetMarkups();
    }

    return ret;
}

CSmartRewards::CSmartRewards(CSmartRewardsDB *prewardsdb) : pdb(prewardsdb)
{

}

bool CSmartRewards::GetLastBlock(CSmartRewardsBlock &block)
{
    LOCK(csDb);
    // Read the last block stored in the rewards database.
    return pdb->ReadLastBlock(block);
}

bool CSmartRewards::GetRound(const int number, CSmartRewardsRound &round)
{

}

bool CSmartRewards::GetCurrentRound(CSmartRewardsRound &round)
{
    LOCK(csDb);
    return pdb->ReadCurrentRound(round);
}

bool CSmartRewards::GetRewardRounds(std::vector<CSmartRewardsRound> &vect)
{
    LOCK(csDb);
    return pdb->ReadRewardRounds(vect);
}

bool CSmartRewards::UpdateRound(const CSmartRewardsRound &round)
{
    LOCK(csDb);
    return pdb->WriteRound(round);
}

bool CSmartRewards::UpdateCurrentRound(const CSmartRewardsRound &round)
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

    std::vector<CSmartRewardsRound> rounds;
    CSmartRewardsRound currentRound;
    prewards->GetRewardRounds(rounds);

    if( prewards->GetCurrentRound(currentRound)){
        LogPrintf("Current Round: %s\n",currentRound.ToString());
    }

    LogPrintf("Finished rounds:");

    BOOST_FOREACH(CSmartRewardsRound r, rounds) {
        LogPrintf("%s\n",r.ToString());
    }

    CSmartRewardsBlock currentBlock;

    // Get the last written block of the
    // rewards database.
    if(!prewards->GetLastBlock(currentBlock)){
        // If there is no one available yet
        // Use -1 to get 0 as start below.
        currentBlock.nHeight = -1;
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

    int64_t nTimeTotal = 0;
    int64_t nTimeUpdateRewards = 0;
    int64_t nTimeUpdateRewardsTotal = 0;
    int64_t nCountUpdateRewards = 0;

    // Cache n blocks before the sync (leveldb batch write).
    const int64_t nCacheBlocks = 50;

    while (true)
    {

        int64_t nTime1 = GetTimeMicros();
        {
            LOCK(cs_main);
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

            // Use it as next reward.
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

        CSmartRewardsUpdateResult result;
        // Process the block!
        bool sync= currentBlock.nHeight > 0 && !(currentBlock.nHeight % nCacheBlocks);
        if(!prewards->Update(nextIndex, chainparams, result, sync)) throw runtime_error(std::string(__func__) + ": rewards update failed");

        // Update the current block to the processed one
        currentBlock = result.block;
        // Next round we use the current of this round as last.
        lastIndex = nextIndex;

        int64_t nTime4 = GetTimeMicros();

        nTimeUpdateRewards += nTime4 - nTime3;
        nTimeUpdateRewardsTotal += nTime4 - nTime3;
        ++nCountUpdateRewards;

        CSmartRewardsRound round;

        if( !prewards->GetCurrentRound(round) ){

            if( lastIndex->GetBlockTime() > 1500966000){
                // Create the very first smartrewards round.
                CSmartRewardsRound next(1,lastIndex->nHeight + 1);

                // Evaluate the start of smartrewards and update the next rounds parameter.
                prewards->EvaluateCurrentRound(next);

                next.endBlockTime = 1503644400;

                prewards->UpdateCurrentRound(next);
            }

            continue;

        }else{

            bool updateRound = false;

            // If we created the new round in the last loop set the parameter for the next round
            if( !round.startBlockTime ){

                round.startBlockTime = lastIndex->GetBlockTime();

                struct tm * ptm;
                time_t start = (time_t)round.startBlockTime;
                // Calculate the ending timestamp for the next round
                ptm = gmtime( &start );

                if( ++ptm->tm_mon > 11 ){
                    ptm->tm_mon = 0;
                    ptm->tm_year += 1;
                }

                ptm->tm_mday = 25;
                ptm->tm_hour = 7;
                ptm->tm_min = 0;
                ptm->tm_sec = 0;

                setenv("TZ", "UTC", 1);

                round.endBlockTime = mktime(ptm);
                // Estimate the block, gets updated on the end of the round to the real one.
                round.endBlockHeight = (round.endBlockTime - round.startBlockTime) / 55;

                updateRound = true;
            }

            // If there were disqualification during the last block processing
            // update the current round stats.
            if( result.disqualifiedEntries || result.disqualifiedSmart ){
                round.eligibleEntries -= result.disqualifiedEntries;
                round.eligibleSmart -= result.disqualifiedSmart;
                updateRound = true;
            }

            if( updateRound ) prewards->UpdateCurrentRound(round);

        }

        // If just hit the next round threshold
        if(lastIndex->GetBlockTime() > round.endBlockTime){

            // Create the next round.
            CSmartRewardsRound next(round.number + 1,lastIndex->nHeight + 1);
            // Evaluate the round and update the next rounds parameter.
            prewards->EvaluateCurrentRound(next);
            // Write the round to the history
            round.endBlockHeight = lastIndex->nHeight;
            round.endBlockTime = lastIndex->GetBlockTime();
            prewards->UpdateRound(round);
            // Write it to the db.
            prewards->UpdateCurrentRound(next);
        }

        int64_t nTime5 = GetTimeMicros();

        // Check if the reward list is synced
        if(1){
            //If yes we notify the UI on each new block.

        }else{
            // If notify the UI every 100 blocks to let it update the
            // loading screen.

        }

        int64_t nTime6 = GetTimeMicros(); nTimeTotal += nTime6 - nTime1;

        LogPrintf("Get index [%d]: %.2fms\n",currentBlock.nHeight, (nTime2 - nTime1) * 0.001);
        LogPrintf("Next index [%d]: %.2fms\n",currentBlock.nHeight, (nTime3 - nTime2) * 0.001);
        LogPrintf("Update rewards [%d]: %.2fms / %.2fms [%.2fs]\n",currentBlock.nHeight, (nTime4 - nTime3) * 0.001, (nTimeUpdateRewardsTotal/nCountUpdateRewards) * 0.001, nTimeUpdateRewards * 0.000001);
        LogPrintf("Evaluate round [%d]: %.2fms\n",currentBlock.nHeight, (nTime5 - nTime4) * 0.001);
        LogPrintf("Notify UI [%d]: %.2fms\n",currentBlock.nHeight, (nTime6 - nTime5) * 0.001);
        LogPrintf("Total: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);

    }
}
