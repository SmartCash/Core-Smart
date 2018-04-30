#include "smartrewards/rewards.h"
#include "validation.h"

CSmartRewards *prewards = NULL;


bool CSmartRewards::Verify()
{

}

bool CSmartRewards::Update(CBlockIndex *pindexNew, const CChainParams& chainparams) {

    bool result = true;

    CBlockIndex * pIndex = pindexNew;

    // Add to the smartrewards database
      if(pIndex->nHeight > 10){

          //Reset marked entries if there are any
          ResetMarkups();

//         boost::this_thread::interruption_point();

         // Move back 10 blocks from current
         for(int i = 0; i < 10; i++) pIndex = pIndex->pprev;

         CBlock block;
         ReadBlockFromDisk(block, pIndex, chainparams.GetConsensus());

         // Block time to day of month
         time_t rTime = block.GetBlockTime();
         tm *ltm = localtime(&rTime);
         int dayOfMonth = ltm->tm_mday;
         ostringstream o;
         o << block.GetBlockTime();
         string relativeTime = o.str();

         uint16_t rewardsRound;
         int64_t roundStart;

         CTransaction rTx;
         uint256 rBlockHash;
         std::vector<CSmartRewardEntry>updateEntries();
         std::vector<CSmartRewardEntry>removeEntries();

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
         result = SyncMarkups(CSmartRewardsBlock(pIndex->nHeight, blockHash, block.GetBlockTime()));
      }

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
}


bool CSmartRewards::SyncMarkups(const CSmartRewardsBlock &block)
{
    bool res = pdb->SyncBlock(block,updateEntries,removeEntries);
    ResetMarkups();
    return res;
}

CSmartRewards::CSmartRewards(CSmartRewardsDB *prewardsdb) : pdb(prewardsdb)
{

}

