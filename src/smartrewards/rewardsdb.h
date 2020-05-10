// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REWARDSDB_H
#define REWARDSDB_H

#include "dbwrapper.h"
#include "amount.h"
#include "chain.h"
#include "coins.h"
#include "base58.h"
#include "smarthive/hive.h"

static constexpr uint8_t REWARDS_DB_VERSION = 0x0A;

//! Compensate for extra memory peak (x1.5-x1.9) at flush time.
static constexpr int REWARDS_DB_PEAK_USAGE_FACTOR = 2;
//! -rewardsdbcache default (MiB)
static const int64_t nRewardsDefaultDbCache = 80;
//! max. -rewardsdbcache (MiB)
static const int64_t nRewardsMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;

class CSmartRewardBlock;
class CSmartRewardEntry;
class CSmartRewardRound;
class CSmartRewardResultEntry;
class CSmartRewardTransaction;
class CSmartRewardsCache;

typedef std::vector<CSmartRewardBlock> CSmartRewardBlockList;
typedef std::vector<CSmartRewardEntry> CSmartRewardEntryList;
typedef std::map<uint16_t, CSmartRewardRound> CSmartRewardRoundMap;
typedef std::vector<CSmartRewardResultEntry> CSmartRewardResultEntryList;
typedef std::vector<CSmartRewardResultEntry*> CSmartRewardResultEntryPtrList;

typedef std::map<uint256, CSmartRewardTransaction> CSmartRewardTransactionMap;
typedef std::map<CSmartAddress, CSmartRewardEntry*> CSmartRewardEntryMap;

class CSmartRewardTransaction
{

public:
    int blockHeight;
    uint256 hash;
    CSmartRewardTransaction(){blockHeight = 0; hash = uint256();}
    CSmartRewardTransaction(int height, const uint256 &hash) : blockHeight(height), hash(hash) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(blockHeight);
        READWRITE(hash);
    }

    friend bool operator==(const CSmartRewardTransaction& a, const CSmartRewardTransaction& b)
    {
        return (a.hash == b.hash);
    }

    friend bool operator!=(const CSmartRewardTransaction& a, const CSmartRewardTransaction& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};
class CSmartRewardBlock
{

public:
    int nHeight;
    uint256 nHash;
    int64_t nTime;
    CSmartRewardBlock() : nHeight(0), nHash(uint256()), nTime(0) {}
    CSmartRewardBlock(int nHeight, const uint256* pHash, int64_t nTime) : nHeight(nHeight), nHash(*pHash), nTime(nTime) {}
    CSmartRewardBlock(int nHeight, const uint256 nHash, int64_t nTime) : nHeight(nHeight), nHash(nHash), nTime(nTime) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nHeight);
        READWRITE(nHash);
        READWRITE(nTime);
    }

    friend bool operator==(const CSmartRewardBlock& a, const CSmartRewardBlock& b)
    {
        return (a.nHeight == b.nHeight);
    }

    friend bool operator!=(const CSmartRewardBlock& a, const CSmartRewardBlock& b)
    {
        return !(a == b);
    }

    friend bool operator<(const CSmartRewardBlock& a, const CSmartRewardBlock& b)
    {
        return (a.nHeight < b.nHeight);
    }

    std::string ToString() const;

    bool IsValid() const { return nHeight > 0; }
};

class CSmartRewardRound
{

    /* Memory only */
    int nPayeeCount;
    int nRewardBlocks;
    int nLastRoundBlock;

public:
    uint16_t number;
    int64_t startBlockHeight;
    int64_t startBlockTime;
    int64_t endBlockHeight;
    int64_t endBlockTime;

    int64_t eligibleEntries;
    CAmount eligibleSmart;
    int64_t disqualifiedEntries;
    CAmount disqualifiedSmart;

    int64_t nBlockPayees;
    int64_t nBlockInterval;

    CAmount rewards;
    double percent;

    CSmartRewardRound() {
        number = 0;
        startBlockHeight = INT_MAX;
        startBlockTime = INT_MAX;
        endBlockHeight = INT_MAX;
        endBlockTime = INT_MAX;
        eligibleEntries = 0;
        eligibleSmart = 0;
        disqualifiedEntries = 0;
        disqualifiedSmart = 0;
        nBlockPayees = 0;
        nBlockInterval = 0;
        rewards = 0;
        percent = 0;
        nPayeeCount = 0;
        nRewardBlocks = 0;
        nLastRoundBlock = 0;
    }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(number);
        READWRITE(startBlockHeight);
        READWRITE(startBlockTime);
        READWRITE(endBlockHeight);
        READWRITE(endBlockTime);
        READWRITE(eligibleEntries);
        READWRITE(eligibleSmart);
        READWRITE(disqualifiedEntries);
        READWRITE(disqualifiedSmart);
        READWRITE(nBlockPayees);
        READWRITE(nBlockInterval);
        READWRITE(rewards);
        READWRITE(percent);

        if( ser_action.ForRead())
            UpdatePayoutParameter();
    }

    friend bool operator<(const CSmartRewardRound& a, const CSmartRewardRound& b)
    {
        return a.number < b.number;
    }

    void UpdatePayoutParameter();

    int GetPayeeCount() const { return nPayeeCount; }
    int GetRewardBlocks() const { return nRewardBlocks; }
    int GetLastRoundBlock() const { return nLastRoundBlock; }
    bool Is_1_3() const { return number >= Params().GetConsensus().nRewardsFirst_1_3_Round; }

    std::string ToString() const;
};

class CSmartRewardEntry
{

public:

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(id);
        READWRITE(balance);
        READWRITE(balanceAtStart);
        READWRITE(balanceEligible);
        READWRITE(disqualifyingTx);
        READWRITE(fDisqualifyingTx);
        READWRITE(activationTx);
        READWRITE(fActivated);
        READWRITE(smartnodePaymentTx);
        READWRITE(fSmartnodePaymentTx);
        READWRITE(bonusLevel);
    }

    enum BonusLevel {
      NoBonus = 0,
      TwoMonthsBonus,
      FourMonthsBonus,
      SixMonthsBonus
    };

    CSmartAddress id;
    CAmount balance;
    CAmount balanceAtStart;
    CAmount balanceEligible;
    uint256 disqualifyingTx;
    bool fDisqualifyingTx;
    uint256 activationTx;
    bool fActivated;
    uint256 smartnodePaymentTx;
    bool fSmartnodePaymentTx;
    uint8_t bonusLevel;

    CSmartRewardEntry() : id(CSmartAddress()),
                          balance(0), balanceAtStart(0), balanceEligible(0),
                          disqualifyingTx(uint256()), fDisqualifyingTx(false),
                          activationTx(uint256()), fActivated(false),
                          smartnodePaymentTx(uint256()), fSmartnodePaymentTx(false),
                          bonusLevel(NoBonus) {}
    CSmartRewardEntry(const CSmartAddress &address) : id(address),
                          balance(0), balanceAtStart(0), balanceEligible(0),
                          disqualifyingTx(uint256()), fDisqualifyingTx(false),
                          activationTx(uint256()), fActivated(false),
                          smartnodePaymentTx(uint256()), fSmartnodePaymentTx(false),
                          bonusLevel(NoBonus) {}

    friend bool operator==(const CSmartRewardEntry& a, const CSmartRewardEntry& b)
    {
        return (a.id == b.id);
    }

    friend bool operator!=(const CSmartRewardEntry& a, const CSmartRewardEntry& b)
    {
        return !(a == b);
    }

    std::string GetAddress() const;
    void SetNull();
    std::string ToString() const;
    bool IsEligible();
};

class CSmartRewardResultEntry
{

public:

    CSmartRewardEntry entry;
    CAmount reward;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(entry);
        READWRITE(reward);
    }

    CSmartRewardResultEntry(){}

    CSmartRewardResultEntry(CSmartRewardEntry *entry, CAmount nReward) :
        entry(*entry), reward(nReward){}

    friend bool operator==(const CSmartRewardResultEntry& a, const CSmartRewardResultEntry& b)
    {
        return (a.entry.id == b.entry.id);
    }

    friend bool operator!=(const CSmartRewardResultEntry& a, const CSmartRewardResultEntry& b)
    {
        return !(a == b);
    }

    friend bool operator<(const CSmartRewardResultEntry& a, const CSmartRewardResultEntry& b)
    {
        // TBD, verify this sort is fast/unique
        int cmp = a.entry.id.Compare(b.entry.id);
        return cmp < 0 || (cmp == 0 && a.reward < b.reward);
    }

    std::string GetAddress() const;
    std::string ToString() const;

    arith_uint256 CalculateScore(const uint256& blockHash);
};


/** Access to the rewards database (rewards/) */
class CSmartRewardsDB : public CDBWrapper
{
public:
    CSmartRewardsDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    ~CSmartRewardsDB() {Sync();}
private:
    CSmartRewardsDB(const CSmartRewardsDB&);
    void operator=(const CSmartRewardsDB&);

public:

    bool Verify(int& lastBlockHeight);

    bool ReadBlock(const int nHeight, CSmartRewardBlock &block);
    bool ReadLastBlock(CSmartRewardBlock &block);

    bool ReadTransaction(const uint256 hash, CSmartRewardTransaction &transaction);

    bool ReadRound(const int16_t number, CSmartRewardRound &round);
    bool ReadRounds(CSmartRewardRoundMap &rounds);

    bool ReadCurrentRound(CSmartRewardRound &round);

    bool ReadRewardEntry(const CSmartAddress &id, CSmartRewardEntry &entry);
    bool ReadRewardEntries(CSmartRewardEntryMap &entries);

    bool ReadRewardRoundResults(const int16_t round, CSmartRewardResultEntryList &results);
    bool ReadRewardRoundResults(const int16_t round, CSmartRewardResultEntryPtrList &results);
    bool ReadRewardPayouts(const int16_t round, CSmartRewardResultEntryList &payouts);
    bool ReadRewardPayouts(const int16_t round, CSmartRewardResultEntryPtrList &payouts);

    bool SyncCached(const CSmartRewardsCache &cache);
    bool FinalizeRound(const CSmartRewardRound &current, const CSmartRewardRound &next, const CSmartRewardEntryList &entries, const CSmartRewardResultEntryList &results);
    bool UndoFinalizeRound(const CSmartRewardRound &current, const CSmartRewardResultEntryList &results);
};



#endif // REWARDSDB_H
