// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REWARDS_H
#define REWARDS_H

#include "sync.h"

#include <smartrewards/rewardsdb.h>
#include "consensus/consensus.h"

using namespace std;

#define REWARDS_CACHE_ENTRIES_DEFAULT 50000

static const CAmount SMART_REWARDS_MIN_BALANCE = 1000 * COIN;

// Minimum distance of the last processed block compared to the current chain
// height to assume the rewards are synced.
const int64_t nRewardsSyncDistance = 150;
// Number of blocks we update the SmartRewards UI when we are in the sync process
const int64_t nRewardsUISyncUpdateRate = 500;

// First automated round on mainnet
const int64_t nRewardsFirstAutomatedRound = 13;

// Timestamps of the first round's start and end on mainnet
const int64_t nFirstRoundStartTime = 1500966000;
const int64_t nFirstRoundEndTime = 1503644400;
const int64_t nFirstRoundStartBlock = 1;
const int64_t nFirstRoundEndBlock = 60001;

// Timestamps of the first round's start and end on testnet
const int64_t nRewardsSyncDistance_Testnet = 60;
const int64_t nFirstTxTimestamp_Testnet = 1527192589;
const int64_t nFirstRoundStartTime_Testnet = nFirstTxTimestamp_Testnet;
const int64_t nFirstRoundEndTime_Testnet = nFirstRoundStartTime_Testnet + (2*60*60);
const int64_t nFirstRoundStartBlock_Testnet = TESTNET_V1_2_PAYMENTS_HEIGHT;
const int64_t nFirstRoundEndBlock_Testnet = nFirstRoundStartBlock_Testnet + 1000;

void ThreadSmartRewards(bool fRecreate = false);
CAmount CalculateRewardsForBlockRange(int64_t start, int64_t end);

extern CCriticalSection cs_rewardscache;
extern CCriticalSection cs_rewardsdb;

extern size_t nCacheRewardEntries;

struct CSmartRewardsUpdateResult
{
    int64_t disqualifiedEntries;
    int64_t disqualifiedSmart;
    int64_t qualifiedEntries;
    int64_t qualifiedSmart;
    CSmartRewardBlock block;
    CSmartRewardsUpdateResult() : disqualifiedEntries(0), disqualifiedSmart(0), qualifiedEntries(0), qualifiedSmart(0), block() {}
    CSmartRewardsUpdateResult(const int nHeight, const uint256* pBlockHash, const int64_t nBlockTime) : disqualifiedEntries(0), disqualifiedSmart(0), qualifiedEntries(0), qualifiedSmart(0), block(nHeight, pBlockHash, nBlockTime) { }

    bool IsValid() const { return block.IsValid(); }
};

struct CSmartRewardsRoundResult
{
    CSmartRewardRound round;
    CSmartRewardResultEntryPtrList results;
    CSmartRewardResultEntryPtrList payouts;
    bool fSynced;
    CSmartRewardsRoundResult(){fSynced = false;}

    void Clear();
};

class CSmartRewardsCache
{
    int chainHeight;
    int rewardHeight;

    CSmartRewardBlock block;
    CSmartRewardRound round;
    CSmartRewardRoundList rounds;
    CSmartRewardTransactionMap addTransactions;
    CSmartRewardTransactionMap removeTransactions;
    CSmartRewardEntryMap entries;
    CSmartRewardsRoundResult *result;

public:

    CSmartRewardsCache() : block(), round(), rounds(), addTransactions(), removeTransactions(), entries(), result(nullptr) { }
    ~CSmartRewardsCache();

    unsigned long EstimatedSize();

    void Load(const CSmartRewardBlock &block, const CSmartRewardRound &round, const CSmartRewardRoundList &rounds);

    bool NeedsSync();
    void Clear();

    void SetCurrentBlock(const CSmartRewardBlock &currentBlock);
    void SetCurrentRound(const CSmartRewardRound &currentRound);
    void SetResult(CSmartRewardsRoundResult *pResult);

    void ApplyRoundUpdateResult(const CSmartRewardsUpdateResult &result);
    void UpdateRoundParameter(int64_t nBlockPayees, int64_t nBlockInterval, double dPercent);
    void UpdateRoundEnd(int nBlockHeight, int64_t nBlockTime);
    void UpdateHeights(const int nHeight, const int nRewardHeight);

    const CSmartRewardBlock* GetCurrentBlock() const { return &block; }
    const CSmartRewardRound* GetCurrentRound() const { return &round; }
    const CSmartRewardRoundList* GetRounds() const { return &rounds; }
    const CSmartRewardTransactionMap* GetAddedTransactions() const { return &addTransactions; }
    const CSmartRewardTransactionMap* GetRemovedTransactions() const { return &removeTransactions; }
    const CSmartRewardEntryMap* GetEntries() const { return &entries; }
    const CSmartRewardsRoundResult* GetLastRoundResult() const { return result; }

    void AddFinishedRound(const CSmartRewardRound &round);
    void AddTransaction(const CSmartRewardTransaction &transaction);
    void RemoveTransaction(const CSmartRewardTransaction &transaction);
    void AddEntry(CSmartRewardEntry *entry);
};

class CSmartRewards
{
    CSmartRewardsDB * pdb;
    CSmartRewardsCache cache;

    mutable CCriticalSection csRounds;

    void UpdateRoundParameter(const CSmartRewardsUpdateResult &result);

    bool ReadRewardEntry(const CSmartAddress &id, CSmartRewardEntry &entry);
    bool GetRewardEntries(CSmartRewardEntryMap &entries);
public:

    CSmartRewards(CSmartRewardsDB *prewardsdb);
    ~CSmartRewards() { delete pdb; }
    void Lock();
    bool IsLocked();

    bool GetLastBlock(CSmartRewardBlock &block);
    bool GetTransaction(const uint256 hash, CSmartRewardTransaction &transaction);
    const CSmartRewardRound* GetCurrentRound();
    const CSmartRewardRoundList* GetRewardRounds();

    void UpdateHeights(const int nHeight, const int nRewardHeight);
    bool Verify();
    bool NeedsCacheWrite();
    bool SyncCached();
    bool IsSynced();
    double GetProgress();

    int GetBlocksPerRound(const int nRound);

    bool Update(CBlockIndex *pindexNew, const CChainParams& chainparams, const int nCurrentRound, CSmartRewardsUpdateResult &result);
    bool UpdateRound(const CSmartRewardRound &round);

    void ProcessInput(const CTransaction &tx, const CTxOut &in, CSmartAddress **voteProofCheck, CAmount &nVoteProofIn, uint32_t nCurrentRound, CSmartRewardsUpdateResult &result);
    void ProcessOutput(const CTransaction &tx, const CTxOut &out, CSmartAddress *voteProofCheck, CAmount nVoteProofIn, uint32_t nCurrentRound, int nHeight, CSmartRewardsUpdateResult &result);

    void UndoInput(const CTransaction &tx, const CTxOut &in, uint32_t nCurrentRound, CSmartRewardsUpdateResult &result);
    void UndoOutput(const CTransaction &tx, const CTxOut &out, CSmartAddress *voteProofCheck, CAmount &nVoteProofIn, uint32_t nCurrentRound, CSmartRewardsUpdateResult &result);

    bool ProcessTransaction(CBlockIndex* pIndex, const CTransaction& tx, int nCurrentRound);
    void UndoTransaction(CBlockIndex* pIndex, const CTransaction& tx, CCoinsViewCache& coins, const CChainParams& chainparams, CSmartRewardsUpdateResult &result);

    bool CommitBlock(CBlockIndex* pIndex, const CSmartRewardsUpdateResult& result);
    bool CommitUndoBlock(CBlockIndex* pIndex, const CSmartRewardsUpdateResult& result);

    bool GetRewardEntry(const CSmartAddress &id, CSmartRewardEntry *&entry, bool fCreate);

    void EvaluateRound(CSmartRewardRound &next);
    bool StartFirstRound(const CSmartRewardRound &next, const CSmartRewardEntryList &entries);
    bool FinalizeRound(const CSmartRewardRound &current, const CSmartRewardRound &next, const CSmartRewardEntryList &entries, const CSmartRewardResultEntryList &results);
    bool UndoFinalizeRound(const CSmartRewardRound &current, const CSmartRewardResultEntryList &results);

    bool GetRewardRoundResults(const int16_t round, CSmartRewardResultEntryList &results);
    const CSmartRewardsRoundResult* GetLastRoundResult();
    bool GetRewardPayouts(const int16_t round, CSmartRewardResultEntryList &payouts);
    bool GetRewardPayouts(const int16_t round, CSmartRewardResultEntryPtrList &payouts);

};

/** Global variable that points to the active rewards object (protected by cs_main) */
extern CSmartRewards *prewards;
extern bool fSmartRewardsRunning;

#endif // REWARDS_H
