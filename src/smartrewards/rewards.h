// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REWARDS_H
#define REWARDS_H

#include "sync.h"

#include <smartrewards/rewardsdb.h>
#include "consensus/consensus.h"

using namespace std;

static const CAmount SMART_REWARDS_MIN_BALANCE = 1000 * COIN;
// Cache max. n prepared entries before the sync (leveldb batch write).
const int64_t nCacheEntires = 8000;
// Minimum number of confirmations to process a block for the reward database.
const int64_t nRewardsConfirmations = 100;
// Minimum distance of the last processed block compared to the current chain
// height to assume the rewards are synced.
const int64_t nRewardsSyncDistance = nRewardsConfirmations + 10;
// Number of blocks we update the SmartRewards UI when we are in the sync process
const int64_t nRewardsUISyncUpdateRate = 500;
// Number of blocks we update the SmartRewards UI when we are in the sync process
const int64_t nRewardsBlocksPerRound_1_2 = 47500;
const int64_t nRewardsBlocksPerRound_1_3 = 142500;
const int64_t nRewardsFirstAutomatedRound = 13;
const int64_t nRewardsFirst_1_3_Round = 25;

// Timestamps of the first round's start and end on mainnet
const int64_t nFirstRoundStartTime = 1500966000;
const int64_t nFirstRoundEndTime = 1503644400;
const int64_t nFirstRoundStartBlock = 1;
const int64_t nFirstRoundEndBlock = 60001;

// Timestamps of the first round's start and end on testnet
const int64_t nRewardsConfirmations_Testnet = 50;
const int64_t nRewardsSyncDistance_Testnet = nRewardsConfirmations_Testnet + 10;
const int64_t nRewardsBlocksPerRound_1_2_Testnet = 1000;
const int64_t nRewardsBlocksPerRound_1_3_Testnet = 5000;
const int64_t nFirstTxTimestamp_Testnet = 1527192589;
const int64_t nFirstRoundStartTime_Testnet = nFirstTxTimestamp_Testnet;
const int64_t nFirstRoundEndTime_Testnet = nFirstRoundStartTime_Testnet + (2*60*60);
const int64_t nFirstRoundStartBlock_Testnet = TESTNET_V1_2_PAYMENTS_HEIGHT;
const int64_t nFirstRoundEndBlock_Testnet = nFirstRoundStartBlock_Testnet + nRewardsBlocksPerRound_1_2_Testnet;
const int64_t nRewardsFirst_1_3_Round_Testnet = 315;


void ThreadSmartRewards(bool fRecreate = false);
CAmount CalculateRewardsForBlockRange(int64_t start, int64_t end);

extern CCriticalSection cs_rewardsdb;
extern CCriticalSection cs_rewardrounds;

struct CSmartRewardsUpdateResult
{
    int64_t disqualifiedEntries;
    int64_t disqualifiedSmart;
    CSmartRewardBlock block;
    CSmartRewardsUpdateResult(const int nHeight, const uint256* pBlockHash, const int64_t nBlockTime) : disqualifiedEntries(0), disqualifiedSmart(0), block(nHeight, pBlockHash, nBlockTime) { }
};

class CSmartRewards
{
    CSmartRewardsDB * pdb;
    CSmartRewardRoundList finishedRounds;
    CSmartRewardRound currentRound;
    CSmartRewardRound lastRound;
    CSmartRewardBlock currentBlock;
    CSmartRewardBlock lastBlock;

    int chainHeight;
    int rewardHeight;

    CSmartRewardBlockList blockEntries;
    CSmartRewardTransactionList transactionEntries;
    CSmartRewardEntryMap rewardEntries;

    mutable CCriticalSection csRounds;

    void UpdatePayoutParameter(CSmartRewardRound &round);

    bool GetCachedRewardEntry(const CSmartAddress &id, CSmartRewardEntry *&entry);
    bool ReadRewardEntry(const CSmartAddress &id, CSmartRewardEntry &entry);
    bool GetRewardEntries(CSmartRewardEntryList &entries);
    bool AddBlock(const CSmartRewardBlock &block, bool sync);
    void AddTransaction(const CSmartRewardTransaction &transaction);
public:

    CSmartRewards(CSmartRewardsDB *prewardsdb);
    ~CSmartRewards() { SyncPrepared(); delete pdb; }
    void Lock();
    bool IsLocked();

    void CatchUp();

    bool GetLastBlock(CSmartRewardBlock &block);
    bool GetTransaction(const uint256 hash, CSmartRewardTransaction &transaction);
    const CSmartRewardRound& GetCurrentRound();
    const CSmartRewardRound &GetLastRound();
    const CSmartRewardRoundList& GetRewardRounds();

    void UpdateHeights(const int nHeight, const int nRewardHeight);
    bool Verify();
    bool SyncPrepared();
    bool IsSynced();
    double GetProgress();
    int GetLastHeight();

    int GetBlocksPerRound(const int nRound);

    bool Update(CBlockIndex *pindexNew, const CChainParams& chainparams, const int nCurrentRound, CSmartRewardsUpdateResult &result);
    bool UpdateRound(const CSmartRewardRound &round);

    void ProcessTransaction(CBlockIndex* pLastIndex, const CTransaction& tx, CCoinsViewCache& coins, const CChainParams& chainparams, CSmartRewardsUpdateResult &result);
//    void ProcessBlock(CBlockIndex* pLastIndex,const CChainParams& chainparams);

    void CommitBlock(CBlockIndex* pIndex, const CSmartRewardsUpdateResult& result);

    bool GetRewardEntry(const CSmartAddress &id, CSmartRewardEntry &entry);

    void EvaluateRound(CSmartRewardRound &current, CSmartRewardRound &next, CSmartRewardEntryList &entries, CSmartRewardSnapshotList &snapshots);
    bool StartFirstRound(const CSmartRewardRound &next, const CSmartRewardEntryList &entries);
    bool FinalizeRound(const CSmartRewardRound &current, const CSmartRewardRound &next, const CSmartRewardEntryList &entries, const CSmartRewardSnapshotList &snapshots);

    bool GetRewardSnapshots(const int16_t round, CSmartRewardSnapshotList &snapshots);
    bool GetRewardPayouts(const int16_t round, CSmartRewardSnapshotList &payouts);
    bool GetRewardPayouts(const int16_t round, CSmartRewardSnapshotPtrList &payouts);
};

/** Global variable that points to the active rewards object (protected by cs_main) */
extern CSmartRewards *prewards;
extern bool fSmartRewardsRunning;

#endif // REWARDS_H
