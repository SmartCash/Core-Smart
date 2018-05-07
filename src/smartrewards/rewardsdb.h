// Copyright (c) 2018 dustinface
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REWARDSDB_H
#define REWARDSDB_H

#include "dbwrapper.h"
#include "amount.h"
#include "chain.h"
#include "coins.h"
#include "base58.h"

//! Compensate for extra memory peak (x1.5-x1.9) at flush time.
static constexpr int REWARDS_DB_VERSIONN = 1;

//! Compensate for extra memory peak (x1.5-x1.9) at flush time.
static constexpr int REWARDS_DB_PEAK_USAGE_FACTOR = 2;
//! -rewardsdbcache default (MiB)
static const int64_t nRewardsDefaultDbCache = 30;
//! max. -rewardsdbcache (MiB)
static const int64_t nRewardsMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;

class CSmartRewardBlock;
class CSmartRewardEntry;
class CSmartRewardRound;
class CSmartRewardPayout;
class CSmartRewardTransaction;

typedef std::vector<CSmartRewardBlock> CSmartRewardBlockList;
typedef std::vector<CSmartRewardEntry> CSmartRewardEntryList;
typedef std::vector<CSmartRewardRound> CSmartRewardRoundList;
typedef std::vector<CSmartRewardPayout> CSmartRewardPayoutList;
typedef std::vector<CSmartRewardTransaction> CSmartRewardTransactionList;

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
        startBlockHeight = 0;
        startBlockTime = 0;
        endBlockHeight = 0;
        endBlockTime = 0;
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

struct CSmartRewardId : public CBitcoinAddress
{
    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vchVersion);
        READWRITE(vchData);
    }

    CSmartRewardId() : CBitcoinAddress() {}
    CSmartRewardId(const std::string &address) : CBitcoinAddress(address) {}
    CSmartRewardId(const CTxDestination &destination) : CBitcoinAddress(destination) {}
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

    CSmartRewardId id;
    CAmount balance;
    CAmount balanceOnStart;
    bool eligible;

    CSmartRewardEntry() : id(CSmartRewardId()),
                          balance(0), balanceOnStart(0),
                          eligible(false) {}

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

class CSmartRewardPayout
{

public:

    CSmartRewardId id;
    CAmount balance;
    CAmount reward;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(id);
        READWRITE(balance);
        READWRITE(reward);
    }

    CSmartRewardPayout(){}

    CSmartRewardPayout(const CSmartRewardEntry &entry, const CSmartRewardRound &round) {
        id = entry.id;
        balance = entry.balanceOnStart;
        reward = CAmount(balance * round.percent);
    }

    friend bool operator==(const CSmartRewardPayout& a, const CSmartRewardPayout& b)
    {
        return (a.id == b.id);
    }

    friend bool operator!=(const CSmartRewardPayout& a, const CSmartRewardPayout& b)
    {
        return !(a == b);
    }

    std::string GetAddress() const;
    std::string ToString() const;
};

/** Access to the rewards database (rewards/) */
class CSmartRewardsDB : public CDBWrapper
{
public:
    CSmartRewardsDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
private:
    CSmartRewardsDB(const CSmartRewardsDB&);
    void operator=(const CSmartRewardsDB&);
public:

    bool Verify();

    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex);

    bool ReadBlock(const int nHeight, CSmartRewardBlock &block);
    bool ReadLastBlock(CSmartRewardBlock &block);

    bool ReadTransaction(const uint256 hash, CSmartRewardTransaction &transaction);

    bool ReadRound(const int number, CSmartRewardRound &round);
    bool WriteRound(const CSmartRewardRound &round);
    bool ReadRewardRounds(CSmartRewardRoundList &vect);

    bool ReadCurrentRound(CSmartRewardRound &round);
    bool WriteCurrentRound(const CSmartRewardRound &round);

    bool ReadRewardEntry(const CSmartRewardId &id, CSmartRewardEntry &entry);
    bool WriteRewardEntry(const CSmartRewardEntry &entry);
    bool RemoveRewardEntry(const CSmartRewardEntry &entry);

    bool ReadRewardEntries(CSmartRewardEntryList &vect);
    bool WriteRewardEntries(const CSmartRewardEntryList &vect);

    bool ReadRewardPayouts(const int16_t round, CSmartRewardPayoutList &payouts);
    bool WriteRewardPayouts(const int16_t round, const CSmartRewardPayoutList &payouts);

    bool SyncBlocks(const CSmartRewardBlockList &blocks, const CSmartRewardEntryList &update, const CSmartRewardEntryList &remove, const CSmartRewardTransactionList &transactions);
    bool FinalizeRound(const CSmartRewardRound &next, const CSmartRewardEntryList &entries);
    bool FinalizeRound(const CSmartRewardRound &current, const CSmartRewardRound &next, const CSmartRewardEntryList &entries, const CSmartRewardPayoutList &payouts);

};



#endif // REWARDSDB_H
