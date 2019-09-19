// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REWARDSPAYMENTS_H
#define REWARDSPAYMENTS_H

#include "smartrewards/rewardsdb.h"
#include "dbwrapper.h"
#include "amount.h"
#include "chain.h"
#include "coins.h"
#include "base58.h"

struct CSmartRewardsRoundResult;

namespace SmartRewardPayments{

typedef enum{
    Valid,
    DatabaseError,
    NotSynced,
    NoRewardBlock,
    InvalidRewardList,
    CoreError
} Result;

CSmartRewardResultEntryPtrList GetPayments(const CSmartRewardsRoundResult *pResult, const int64_t nPayoutDelay, const int nHeight, int64_t blockTime, SmartRewardPayments::Result &result);
CSmartRewardResultEntryPtrList GetPaymentsForBlock(const int nHeight, int64_t blockTime, SmartRewardPayments::Result &result);
SmartRewardPayments::Result Validate(const CBlock& block, const int nHeight, CAmount& smartReward);
void FillPayments(CMutableTransaction& txNew, int nHeight, int64_t prevBlockTime, std::vector<CTxOut>& voutSmartRewards);

}
#endif // REWARDSPAYMENTS_H
