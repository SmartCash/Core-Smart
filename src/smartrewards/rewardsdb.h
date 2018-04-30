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
static const int64_t nRewardsDefaultDbCache = 300;
//! max. -rewardsdbcache (MiB)
static const int64_t nRewardsMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;

class CSmartRewardsBlock
{

public:
    int nHeight;
    uint256 blockHash;
    int64_t blockTime;
    CSmartRewardsBlock(){}
    CSmartRewardsBlock(int height, uint256 &hash, int64_t t) : nHeight(height), blockHash(hash), blockTime(t) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nHeight);
        READWRITE(blockHash);
        READWRITE(blockTime);
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

    CSmartRewardEntry() {}

    void setNull();
    std::string ToString() const;
};

class CSmartRewardsRound
{
    uint16_t index;
    int64_t start;
    int64_t end;
    std::vector<CSmartRewardEntry> eligible;

public:
    CSmartRewardsRound() {}
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

    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex);

    bool ReadLastBlock(CSmartRewardsBlock &block);

    bool ReadRewardEntry(const CScript &pubKey, CSmartRewardEntry &entry);
    bool WriteRewardEntry(const CSmartRewardEntry &entry);
    bool RemoveRewardEntry(const CSmartRewardEntry &entry);

    bool SyncBlock(const CSmartRewardsBlock &block, const std::vector<CSmartRewardEntry> &update, const std::vector<CSmartRewardEntry> &remove);

};


#endif // REWARDSDB_H
