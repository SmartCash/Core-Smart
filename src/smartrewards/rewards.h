// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REWARDS_H
#define REWARDS_H

#include "amount.h"
#include "chain.h"
#include "coins.h"
#include "net.h"
#include "script/script_error.h"
#include "sync.h"
#include "versionbits.h"
#include "timedata.h"
#include "chainparams.h"
#include "txmempool.h"
#include "smartrewards/rewards.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <boost/unordered_map.hpp>
#include <boost/filesystem/path.hpp>

#include <smartrewards/rewardsdb.h>

using namespace std;


class CSmartRewards
{
    CSmartRewardsDB * pdb;

    std::vector<CSmartRewardEntry>updateEntries;
    std::vector<CSmartRewardEntry>removeEntries;

public:

    CSmartRewards(CSmartRewardsDB *prewardsdb);

    bool Verify();
    bool Update(CBlockIndex *pindexNew, const CChainParams& chainparams);
    bool CheckRewardRound();

    void GetRewardEntry(const CScript &pubKey, CSmartRewardEntry &entry, bool &added);
    void MarkForUpdate(const CSmartRewardEntry entry);
    void MarkForRemove(const CSmartRewardEntry entry);
    void ResetMarkups();
    bool SyncMarkups(const CSmartRewardsBlock &block);

};

/** Global variable that points to the active rewards object (protected by cs_main) */
extern CSmartRewards *prewards;

#endif // REWARDS_H
