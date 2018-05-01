#include "smartrewards/rewards.h"
#include "validation.h"
#include "init.h"

CSmartRewards *prewards = NULL;


bool CSmartRewards::Verify()
{
    LOCK(csDb);

    bool ret = true;

    CSmartRewardsBlock last;
    CSmartRewardsBlock verify;

    if(!pdb->ReadLastBlock(last)){
        LogPrintf("CSmartRewards::Verify() No block here yet\n");
        return true;
    }

    LogPrintf("CSmartRewards::Verify() Verify blocks 0 - %d\n", last.nHeight);

    int nHeight = 0;
    while(true){

        if(!pdb->ReadBlock(nHeight++, verify)){
            LogPrintf("CSmartRewards::Verify() block %d missing\n",nHeight);
            return false;
        }

        LogPrintf("CSmartRewards::Verify() %s\n", verify.ToString());

        if(verify == last){
            return true;
        }

    }

    return true;
}

bool CSmartRewards::Update(CBlockIndex *pindexNew, const CChainParams& chainparams, CSmartRewardsBlock &rewardBlock, bool sync) {

    if(fLiteMode) return true; // disable SmartRewards sync in litemode

    LOCK(csDb);

    bool result = true;

    CBlock block;
    ReadBlockFromDisk(block, pindexNew, chainparams.GetConsensus());

//    // Block time to day of month
//    time_t rTime = block.GetBlockTime();
//    tm *ltm = localtime(&rTime);
//    int dayOfMonth = ltm->tm_mday;
//    ostringstream o;
//    o << block.GetBlockTime();
//    string relativeTime = o.str();

//    uint16_t rewardsRound;
//    int64_t roundStart;

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
         rEntry.eligible = false;

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
    rewardBlock = CSmartRewardsBlock(pindexNew->nHeight, blockHash, block.GetBlockTime());

    result = SyncMarkups(rewardBlock, sync);

    return result;
}

bool CSmartRewards::CheckRewardRound()
{

    //         if(!pdb->ReadRewardsRound(&rewardsRound)){
    //             pdb->WriteRewardsRound(1);
    //             rewardsRound = 1;
    //         }

    //         if(!pdb->ReadRoundStart(rewardsRound, &roundStart)){
    //             pdb->WriteRoundStart(rewardsRound, block.GetBlockTime());
    //             roundStart = block.GetBlockTime();
    //         }

    //         // If its the 25th of the month save next rewards round
    //         if(dayOfMonth == 25 && stod(relativeTime) > stod(roundStart)+(86400)){
    //         }

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


void ThreadSmartRewards()
{

    if(fLiteMode) return; // disable SmartRewards sync in litemode

    static bool fOneThread;
    if(fOneThread) return;
    fOneThread = true;

    // Make this thread recognisable as the SmartRewards thread
    RenameThread("smartrewards");

    unsigned int nTick = 0;

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

        // Process the block!
        bool sync= currentBlock.nHeight > 0 && !(currentBlock.nHeight % nCacheBlocks);
        if(!prewards->Update(nextIndex, chainparams, currentBlock, sync)) throw runtime_error(std::string(__func__) + ": rewards update failed");

        // Next round we use the current of this round as last.
        lastIndex = nextIndex;

        int64_t nTime4 = GetTimeMicros();

        nTimeUpdateRewards += nTime4 - nTime3; nTimeTotal += nTime4 - nTime1;
        nTimeUpdateRewardsTotal += nTime4 - nTime3;
        ++nCountUpdateRewards;

        LogPrintf("Get index [%d]: %.2fms\n",currentBlock.nHeight, (nTime2 - nTime1) * 0.001);
        LogPrintf("Next index [%d]: %.2fms\n",currentBlock.nHeight, (nTime3 - nTime2) * 0.001);
        LogPrintf("Update rewards [%d]: %.2fms / %.2fms [%.2fs]\n",currentBlock.nHeight, (nTime4 - nTime3) * 0.001, (nTimeUpdateRewardsTotal/nCountUpdateRewards) * 0.001, nTimeUpdateRewards * 0.000001);
        LogPrintf("Total: %.2fms [%.2fs]\n", (nTime4 - nTime1) * 0.001, nTimeTotal * 0.000001);

    }
}
