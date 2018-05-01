// Copyright (c) 2018 dustinface
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REWARDSDB_H
#define REWARDSDB_H

#include "dbwrapper.h"
#include "amount.h"
#include "chain.h"
#include "coins.h"

//! Compensate for extra memory peak (x1.5-x1.9) at flush time.
static constexpr int REWARDS_DB_PEAK_USAGE_FACTOR = 2;
//! -rewardsdbcache default (MiB)
static const int64_t nRewardsDefaultDbCache = 5;
//! max. -rewardsdbcache (MiB)
static const int64_t nRewardsMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;

class CSmartRewardsBlock
{

public:
    int nHeight;
    uint256 blockHash;
    int64_t blockTime;
    CSmartRewardsBlock(){nHeight = 0; blockHash = uint256(); blockTime = 0;}
    CSmartRewardsBlock(int height, uint256 &hash, int64_t t) : nHeight(height), blockHash(hash), blockTime(t) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nHeight);
        READWRITE(blockHash);
        READWRITE(blockTime);
    }

    friend bool operator==(const CSmartRewardsBlock& a, const CSmartRewardsBlock& b)
    {
        return (a.nHeight == b.nHeight);
    }

    friend bool operator!=(const CSmartRewardsBlock& a, const CSmartRewardsBlock& b)
    {
        return !(a == b);
    }

    friend bool operator<(const CSmartRewardsBlock& a, const CSmartRewardsBlock& b)
    {
        return (a.nHeight < b.nHeight);
    }

    std::string ToString() const;
};

struct CSmartRewardsRound
{
    uint16_t number;
    int64_t startBlockHeight;
    int64_t startBlockTime;
    int64_t endBlockHeight;
    int64_t endBlockTime;

    int64_t eligibleEntries;
    int64_t eligibleSmart;
    double percent;

    CSmartRewardsRound() {
        number = 0;
        startBlockHeight = 0;
        startBlockTime = 0;
        endBlockHeight = 0;
        endBlockTime = 0;
        eligibleEntries = 0;
        eligibleSmart = 0;
        percent = 0;
    }
    CSmartRewardsRound(const int64_t number, const int64_t startBlock) : CSmartRewardsRound() {
        this->number = number;
        startBlockHeight = startBlock;
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
        READWRITE(*(CScriptBase*)(&pubKey));
        READWRITE(balanceLastStart);
        READWRITE(balanceOnStart);
        READWRITE(balance);
        READWRITE(eligible);
    }

    CScript pubKey;
    CAmount balance;
    CAmount balanceOnStart;
    CAmount balanceLastStart;
    bool eligible;

    CSmartRewardEntry() : pubKey(CScript()),
                          balance(0), balanceOnStart(0),
                          balanceLastStart(0), eligible(false) {}

    friend bool operator==(const CSmartRewardEntry& a, const CSmartRewardEntry& b)
    {
        return (a.pubKey == b.pubKey);
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

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CScriptBase*)(&pubKey));
        READWRITE(balance);
        READWRITE(reward);
    }

    CScript pubKey;
    CAmount balance;
    CAmount reward;

    CSmartRewardPayout(){}
    CSmartRewardPayout(const CSmartRewardEntry &entry, const CSmartRewardsRound &round) {
        pubKey = entry.pubKey;
        balance = entry.balanceOnStart;
        reward = (CAmount)(balance * ( 1 + round.percent ));
    }

    friend bool operator==(const CSmartRewardPayout& a, const CSmartRewardPayout& b)
    {
        return (a.pubKey == b.pubKey);
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

    bool ReadBlock(const int nHeight, CSmartRewardsBlock &block);
    bool ReadLastBlock(CSmartRewardsBlock &block);

    bool ReadRound(const int number, CSmartRewardsRound &round);
    bool WriteRound(const CSmartRewardsRound &round);
    bool ReadRewardRounds(std::vector<CSmartRewardsRound> &vect);

    bool ReadCurrentRound(CSmartRewardsRound &round);
    bool WriteCurrentRound(const CSmartRewardsRound &round);

    bool ReadRewardEntry(const CScript &pubKey, CSmartRewardEntry &entry);
    bool WriteRewardEntry(const CSmartRewardEntry &entry);
    bool RemoveRewardEntry(const CSmartRewardEntry &entry);

    bool ReadRewardEntries(std::vector<CSmartRewardEntry> &vect);
    bool WriteRewardEntries(const std::vector<CSmartRewardEntry>&vect);

    bool ReadRewardPayouts(const int64_t round, std::vector<CSmartRewardPayout> &vect);
    bool WriteRewardPayouts(const int64_t round, const std::vector<CSmartRewardPayout>&vect);

    bool Sync(const std::vector<CSmartRewardsBlock> &blocks, const std::vector<CSmartRewardEntry> &update, const std::vector<CSmartRewardEntry> &remove);
};


#endif // REWARDSDB_H
