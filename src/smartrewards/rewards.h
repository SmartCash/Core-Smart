// Copyright (c) 2018 dustinface - SmartCash Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REWARDS_H
#define REWARDS_H

#include "sync.h"

#include <smartrewards/rewardsdb.h>

using namespace std;

static const CAmount SMART_REWARDS_MIN_BALANCE = 1000 * COIN;
// Cache n blocks before the sync (leveldb batch write).
const int64_t nCacheBlocks = 50;
// Minimum distance of the last processed block compared to the current chain
// height to assume the rewards are synced.
const int nRewardsSyncDistance = 30;
// Number of blocks we update the SmartRewards UI when we are in the sync process
const int nRewardsUISyncUpdateRate = 100;

// Timestamps of the first round's start and end
const int64_t firstRoundStartTime = 1500966000;
const int64_t firstRoundEndTime = 1503644400;
const int64_t firstRoundStartBlock = 1;
const int64_t firstRoundEndBlock = 60001;

void ThreadSmartRewards();
CAmount CalculateRewardsForBlockRange(int64_t start, int64_t end);

struct CSmartRewardsUpdateResult
{
    int64_t disqualifiedEntries;
    int64_t disqualifiedSmart;
    CSmartRewardBlock block;
    CSmartRewardsUpdateResult() : disqualifiedEntries(0), disqualifiedSmart(0),block() {}
};

class CSmartRewards
{
    CSmartRewardsDB * pdb;
    int chainHeight;
    int rewardHeight;

    CSmartRewardBlockList blockEntries;
    CSmartRewardTransactionList transactionEntries;
    CSmartRewardEntryList updateEntries;
    CSmartRewardEntryList removeEntries;

    mutable CCriticalSection csDb;

    void PrepareForUpdate(const CSmartRewardEntry &entry);
    void PrepareForRemove(const CSmartRewardEntry &entry);
    bool AddBlock(const CSmartRewardBlock &block, bool sync);
    void AddTransaction(const CSmartRewardTransaction &transaction);
public:

    CSmartRewards(CSmartRewardsDB *prewardsdb) : pdb(prewardsdb) {}

    bool GetLastBlock(CSmartRewardBlock &block);
    bool GetTransaction(const uint256 hash, CSmartRewardTransaction &transaction);
    bool GetCurrentRound(CSmartRewardRound &round);
    bool GetRewardRounds(CSmartRewardRoundList &vect);

    void UpdateHeights(const int nHeight, const int nRewardHeight);
    bool Verify();
    bool SyncPrepared();
    bool IsSynced();
    double GetProgress();

    bool Update(CBlockIndex *pindexNew, const CChainParams& chainparams, CSmartRewardsUpdateResult &result, bool sync);
    bool UpdateCurrentRound(const CSmartRewardRound &round);
    bool UpdateRound(const CSmartRewardRound &round);

    bool GetRewardEntry(const CSmartRewardId &id, CSmartRewardEntry &entry);
    void GetRewardEntry(const CSmartRewardId &id, CSmartRewardEntry &entry, bool &added);
    bool GetRewardEntries(CSmartRewardEntryList &entries);
    void EvaluateRound(CSmartRewardRound &current, CSmartRewardRound &next, CSmartRewardEntryList &entries, CSmartRewardPayoutList &payouts);
    bool FinalizeRound(const CSmartRewardRound &next, const CSmartRewardEntryList &entries);
    bool FinalizeRound(const CSmartRewardRound &current, const CSmartRewardRound &next, const CSmartRewardEntryList &entries, const CSmartRewardPayoutList &payouts);

    bool GetRewardPayouts(const int16_t round, CSmartRewardPayoutList &payouts);
};

/** Global variable that points to the active rewards object (protected by cs_main) */
extern CSmartRewards *prewards;

#endif // REWARDS_H
