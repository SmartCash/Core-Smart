// Copyright (c) 2018 dustinface - SmartCash Developer
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

static constexpr uint8_t REWARDS_DB_VERSION = 0x01;

//! Compensate for extra memory peak (x1.5-x1.9) at flush time.
static constexpr int REWARDS_DB_PEAK_USAGE_FACTOR = 2;
//! -rewardsdbcache default (MiB)
static const int64_t nRewardsDefaultDbCache = 80;
//! max. -rewardsdbcache (MiB)
static const int64_t nRewardsMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;

class CSmartRewardBlock;
class CSmartRewardEntry;
class CSmartRewardRound;
class CSmartRewardSnapshot;
class CSmartRewardTransaction;

typedef std::vector<CSmartRewardBlock> CSmartRewardBlockList;
typedef std::vector<CSmartRewardEntry> CSmartRewardEntryList;
typedef std::vector<CSmartRewardRound> CSmartRewardRoundList;
typedef std::vector<CSmartRewardSnapshot> CSmartRewardSnapshotList;
typedef std::vector<CSmartRewardTransaction> CSmartRewardTransactionList;

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
    uint256 blockHash;
    int64_t blockTime;
    CSmartRewardBlock(){nHeight = 0; blockHash = uint256(); blockTime = 0;}
    CSmartRewardBlock(int height, uint256 &hash, int64_t time) : nHeight(height), blockHash(hash), blockTime(time) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nHeight);
        READWRITE(blockHash);
        READWRITE(blockTime);
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
};

class CSmartRewardRound
{
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
        rewards = 0;
        percent = 0;
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
        READWRITE(rewards);
        READWRITE(percent);
    }

    std::string ToString() const;
};

class CSmartRewardEntry
{

public:

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(id);
        READWRITE(balanceOnStart);
        READWRITE(balance);
        READWRITE(eligible);
    }

    CSmartAddress id;
    CAmount balance;
    CAmount balanceOnStart;
    CAmount reward;
    bool eligible;

    CSmartRewardEntry() : id(CSmartAddress()),
                          balance(0), balanceOnStart(0),
                          reward(0), eligible(false) {}
    CSmartRewardEntry(const CSmartAddress &address) : id(address),
                          balance(0), balanceOnStart(0),
                          reward(0), eligible(false) {}

    friend bool operator==(const CSmartRewardEntry& a, const CSmartRewardEntry& b)
    {
        return (a.id == b.id);
    }

    friend bool operator!=(const CSmartRewardEntry& a, const CSmartRewardEntry& b)
    {
        return !(a == b);
    }

    std::string GetAddress() const;
    void setNull();
    std::string ToString() const;
};

class CSmartRewardSnapshot
{

public:

    CSmartAddress id;
    CAmount balance;
    CAmount reward;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(id);
        READWRITE(balance);
        READWRITE(reward);
    }

    CSmartRewardSnapshot(){}

    CSmartRewardSnapshot(const CSmartRewardEntry &entry, const CSmartRewardRound &round) {
        id = entry.id;
        balance = entry.balance;
        reward = entry.eligible ? CAmount(entry.balanceOnStart * round.percent) : 0;
    }

    friend bool operator==(const CSmartRewardSnapshot& a, const CSmartRewardSnapshot& b)
    {
        return (a.id == b.id);
    }

    friend bool operator!=(const CSmartRewardSnapshot& a, const CSmartRewardSnapshot& b)
    {
        return !(a == b);
    }

    friend bool operator<(const CSmartRewardSnapshot& a, const CSmartRewardSnapshot& b)
    {
        // TBD, verify this sort is fast/unique
        int cmp = a.id.Compare(b.id);
        return cmp < 0 || (cmp == 0 && a.reward < b.reward);
    }

    std::string GetAddress() const;
    std::string ToString() const;
};


/** Access to the rewards database (rewards/) */
class CSmartRewardsDB : public CDBWrapper
{
public:
    CSmartRewardsDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    ~CSmartRewardsDB() { Unlock(); }
private:
    CSmartRewardsDB(const CSmartRewardsDB&);
    void operator=(const CSmartRewardsDB&);

    bool locked;
    void Unlock();
public:

    bool Verify(int& lastBlockHeight);

    void Lock();
    bool IsLocked();

    bool ResetToRound(const int16_t number, const CSmartRewardRound &round, const CSmartRewardEntryList &entries);

    bool ReadBlock(const int nHeight, CSmartRewardBlock &block);
    bool ReadLastBlock(CSmartRewardBlock &block);

    bool ReadTransaction(const uint256 hash, CSmartRewardTransaction &transaction);

    bool ReadRound(const int16_t number, CSmartRewardRound &round);
    bool ReadRounds(CSmartRewardRoundList &vect);

    bool ReadCurrentRound(CSmartRewardRound &round);

    bool ReadRewardEntry(const CSmartAddress &id, CSmartRewardEntry &entry);
    bool ReadRewardEntries(CSmartRewardEntryList &vect);

    bool ReadRewardSnapshots(const int16_t round, CSmartRewardSnapshotList &snapshots);
    bool ReadRewardPayouts(const int16_t round, CSmartRewardSnapshotList &payouts);

    bool SyncBlocks(const CSmartRewardBlockList &blocks, const CSmartRewardRound& current, const CSmartRewardEntryMap &rewards, const CSmartRewardTransactionList &transactions);
    bool StartFirstRound(const CSmartRewardRound &start, const CSmartRewardEntryList &entries);
    bool FinalizeRound(const CSmartRewardRound &current, const CSmartRewardRound &next, const CSmartRewardEntryList &entries, const CSmartRewardSnapshotList &snapshot);

};



#endif // REWARDSDB_H
