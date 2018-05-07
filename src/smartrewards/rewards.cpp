#include "smartrewards/rewards.h"
#include "validation.h"
#include "init.h"
#include <boost/range/irange.hpp>

CSmartRewards *prewards = NULL;


bool isBlacklisted(const CSmartRewardId &id)
{
    BOOST_FOREACH(const CSmartRewardId &b, rewardBlacklist)
        if( id == b ) return true;

    return false;
}
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

        if( GetTransaction(tx.GetHash(),testTx)){
            LogPrintf("Double spended? First occurred in %s - Now in %d",testTx.ToString(), pindexNew->nHeight);
            continue;
        }else{
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
                    // Remove
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

void CSmartRewards::EvaluateRound(CSmartRewardRound &current, CSmartRewardRound &next, CSmartRewardEntryList &entries, CSmartRewardPayoutList &payouts)
{
    LOCK(csDb);
    payouts.clear();

    BOOST_FOREACH(CSmartRewardEntry &e, entries) {

        if( e.eligible ) payouts.push_back(CSmartRewardPayout(e, current));

        e.balanceOnStart = e.balance;

        if( e.balanceOnStart >= SMART_REWARDS_MIN_BALANCE && !isBlacklisted(e.id) ){

            e.eligible = true;

            ++next.eligibleEntries;
            next.eligibleSmart += e.balanceOnStart;
        }else{
            e.eligible = false;
        }

    }

}

bool CSmartRewards::FinalizeRound(const CSmartRewardRound &next, const CSmartRewardEntryList &entries)
{
    LOCK(csDb);
    return pdb->FinalizeRound(next,entries);
}

bool CSmartRewards::FinalizeRound(const CSmartRewardRound &current, const CSmartRewardRound &next, const CSmartRewardEntryList &entries, const CSmartRewardPayoutList &payouts)
{
    LOCK(csDb);
    return pdb->FinalizeRound(current, next, entries, payouts);
}

bool CSmartRewards::GetRewardPayouts(const int16_t round, CSmartRewardPayoutList &payouts)
{
    LOCK(csDb);
    return pdb->ReadRewardPayouts(round, payouts);
}

void CSmartRewards::PrepareForUpdate(const CSmartRewardEntry &entry)
{
    LOCK(csDb);

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

void CSmartRewards::PrepareForRemove(const CSmartRewardEntry &entry)
{
    LOCK(csDb);
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

    // If the entry is already marked for to become updated
    // return this one.
    BOOST_FOREACH(CSmartRewardEntry e, updateEntries) {
        if(e.id == id){
            entry = e;
            return;
        }
    }

    // If the entry is already marked for to become removed
    // return this one.
    BOOST_FOREACH(CSmartRewardEntry e, removeEntries) {
        if(e.id == id){
            entry = e;
            return;
        }
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
    return pdb->ReadRewardRounds(vect);
}

bool CSmartRewards::UpdateRound(const CSmartRewardRound &round)
{
    LOCK(csDb);
    return pdb->WriteRound(round);
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

    CSmartRewardBlock currentBlock;

    // Get the last written block of the
    // rewards database.
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

    int64_t nTimeTotal = 0;
    int64_t nTimeUpdateRewards = 0;
    int64_t nTimeUpdateRewardsTotal = 0;
    int64_t nCountUpdateRewards = 0;

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

        nTimeUpdateRewards += nTime4 - nTime3;
        nTimeUpdateRewardsTotal += nTime4 - nTime3;
        ++nCountUpdateRewards;

        CSmartRewardRound round;
        bool snapshot = false;

        if( !prewards->GetCurrentRound(round) ){

            if( lastIndex->GetBlockTime() > firstRoundStartTime){

                snapshot = true;

                if( !prewards->SyncPrepared() ) throw runtime_error("Could't sync current prepared entries!");

                // Create the very first smartrewards round.
                CSmartRewardRound next;
                next.number = 1;
                next.startBlockTime = lastIndex->GetBlockTime();
                next.startBlockHeight = firstRoundStartBlock;
                next.endBlockTime = firstRoundEndTime;
                // Estimate the block, gets updated on the end of the round to the real one.
                next.endBlockHeight = firstRoundEndBlock;

                CSmartRewardEntryList entries;
                CSmartRewardPayoutList payouts;

                // Get the current entries
                if( !prewards->GetRewardEntries(entries) ) throw runtime_error("Could't read all reward entries!");

                // Evaluate the round and update the next rounds parameter.
                prewards->EvaluateRound(round, next, entries, payouts );

                CalculateRewardRatio(next);

                if( !prewards->FinalizeRound(next, entries) ) throw runtime_error("Could't finalize round!");
            }

            continue;

        }else if( result.disqualifiedEntries || result.disqualifiedSmart ){

            // If there were disqualification during the last block processing
            // update the current round stats.

            round.disqualifiedEntries += result.disqualifiedEntries;
            round.disqualifiedSmart += result.disqualifiedSmart;

            CalculateRewardRatio(round);

            if( !prewards->UpdateCurrentRound(round) ) throw runtime_error("Could't update current round!");
        }

        // If just hit the next round threshold
        if(lastIndex->GetBlockTime() > round.endBlockTime){

            snapshot = true;

            if( !prewards->SyncPrepared() ) throw runtime_error("Could't sync current prepared entries!");

            // Write the round to the history
            round.endBlockHeight = lastIndex->nHeight;
            round.endBlockTime = lastIndex->GetBlockTime();

            CSmartRewardEntryList entries;
            CSmartRewardPayoutList payouts;

            // Create the next round.
            CSmartRewardRound next;
            next.number = round.number + 1;
            next.startBlockTime = round.endBlockTime;
            next.startBlockHeight = round.endBlockHeight + 1;

            struct tm * ptm;
            time_t start = (time_t)next.startBlockTime;
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

            next.endBlockTime = mktime(ptm);
            // Estimate the block, gets updated on the end of the round to the real one.
            next.endBlockHeight = next.startBlockHeight + (next.endBlockTime - next.startBlockTime) / 55;

            if( !prewards->SyncPrepared() ) throw runtime_error("Could't sync current prepared entries!");

            // Get the current entries
            if( !prewards->GetRewardEntries(entries) ) throw runtime_error("Could not read all reward entries!");

            CalculateRewardRatio(round);

            // Evaluate the round and update the next rounds parameter.
            prewards->EvaluateRound(round, next, entries, payouts );

            CalculateRewardRatio(next);

            if( !prewards->FinalizeRound(round, next, entries, payouts) ) throw runtime_error("Could't finalize round!");
        }

        int64_t nTime5 = GetTimeMicros();

        // Check if the reward list is synced
        if(1){
            //If yes we notify the UI on each new block.

        }else{
            // If not notify the UI every 100 blocks to let it update the
            // loading screen.

        }

        int64_t nTime6 = GetTimeMicros(); nTimeTotal += nTime6 - nTime1;

        if(!(currentBlock.nHeight % 1000) || snapshot){
            LogPrintf("[%d] Get index [%d]: %.2fms\n",round.number, currentBlock.nHeight, (nTime2 - nTime1) * 0.001);
            LogPrintf("[%d] Next index [%d]: %.2fms\n",round.number,currentBlock.nHeight, (nTime3 - nTime2) * 0.001);
            LogPrintf("[%d] Update rewards [%d]: %.2fms / %.2fms [%.2fs]\n",round.number,currentBlock.nHeight, (nTime4 - nTime3) * 0.001, (nTimeUpdateRewardsTotal/nCountUpdateRewards) * 0.001, nTimeUpdateRewards * 0.000001);
            LogPrintf("[%d] Evaluate round [%d]: %.2fms\n",round.number,currentBlock.nHeight, (nTime5 - nTime4) * 0.001);
            LogPrintf("[%d] Notify UI [%d]: %.2fms\n",round.number,currentBlock.nHeight, (nTime6 - nTime5) * 0.001);
            LogPrintf("[%d] Total: %.2fms [%.2fs]\n", round.number, (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
        }
    }
}

