#include "smartrewards/rewards.h"
#include "validation.h"
#include "init.h"

CSmartRewards *prewards = NULL;


bool CSmartRewards::Verify()
{

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

      if (i->pubKey == entry.pubKey) {
        i = removeEntries.erase(i);
        break;
      } else {
        ++i;
      }

    }

    for (auto i = updateEntries.begin(); i != updateEntries.end(); ) {

      if (i->pubKey == entry.pubKey) {
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
    for (auto i = removeEntries.begin(); i != removeEntries.end(); ) {

      if (i->pubKey == entry.pubKey) {
        i = removeEntries.erase(i);
        break;
      } else {
        ++i;
      }

    }

    for (auto i = updateEntries.begin(); i != updateEntries.end(); ) {

      if (i->pubKey == entry.pubKey) {
        i = updateEntries.erase(i);
        break;
      } else {
        ++i;
      }

    }

    removeEntries.push_back(entry);
}

void CSmartRewards::GetRewardEntry(const CScript &pubKey, CSmartRewardEntry &entry, bool &added)
{
    added = false;

    BOOST_FOREACH(CSmartRewardEntry e, updateEntries) {
        if(e.pubKey == pubKey){
            entry = e;
            return;
        }
    }

    BOOST_FOREACH(CSmartRewardEntry e, removeEntries) {
        if(e.pubKey == pubKey){
            entry = e;
            return;
        }
    }

    if( !pdb->ReadRewardEntry(pubKey, entry)){

        entry.pubKey = pubKey;
        entry.balanceLastStart = 0;
        entry.balanceOnStart = 0;
        entry.balance = 0;

        //If it was not yet in the db mark it!
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
    if(sync){
        ret = pdb->Sync(blockEntries,updateEntries,removeEntries);
        ResetMarkups();
    }else{
        blockEntries.push_back(block);
    }

    return ret;
}

CSmartRewards::CSmartRewards(CSmartRewardsDB *prewardsdb) : pdb(prewardsdb)
{

}

bool CSmartRewards::GetLastBlock(CSmartRewardsBlock &block)
{
    LOCK(csDb);

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

    if(!prewards->GetLastBlock(currentBlock)){
        currentBlock.nHeight = 0;
        currentBlock.blockHash = uint256();
        currentBlock.blockTime = 0;
    }

    CBlockIndex *lastIndex = NULL;
    CBlockIndex *nextIndex = NULL;
    CBlockIndex *currentIndex = NULL;
    CChainParams chainparams = Params();

    int64_t nTimeTotal = 0;
    int64_t nTimeUpdateRewards = 0;
    int64_t nTimeUpdateRewardsTotal = 0;
    int64_t nCountUpdateRewards = 0;

    const int64_t nCacheBlocks = 50;

    while (true)
    {

        int64_t nTime1 = GetTimeMicros();
        {
            LOCK(cs_main);
            if(ShutdownRequested()) return;
            currentIndex = chainActive.Tip();
        }
        int64_t nTime2 = GetTimeMicros();

        if(!currentIndex){
            MilliSleep(1000);
            continue;
        }

        if(!lastIndex){
            lastIndex = currentIndex;
            while( lastIndex->nHeight != currentBlock.nHeight){
                lastIndex = lastIndex->pprev;
            }
            nextIndex = lastIndex;
        }else{
            LOCK(cs_main);
            nextIndex = chainActive.Next(lastIndex);
        }

        if( !nextIndex || (currentIndex->nHeight - nextIndex->nHeight) < 10 ){
            lastIndex = NULL;
            MilliSleep(1000);
            continue;
        }

        int64_t nTime3 = GetTimeMicros();

        if(!prewards->Update(nextIndex, chainparams, currentBlock, !(currentBlock.nHeight % nCacheBlocks))) throw runtime_error(std::string(__func__) + ": rewards update failed");

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
