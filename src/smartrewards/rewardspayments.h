// Copyright (c) 2018 dustinface - SmartCash Developer
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


//! Number of blocks to wait until we start to pay the rewards after a cycles end.
static const int64_t nRewardPayoutStartDelay = 200;
//! Number of blocks to wait between reward payout blocks
static const int64_t nRewardPayoutBlockInterval = 5;
//! Number of payouts per rewardblock
static const int64_t nRewardPayoutsPerBlock = 300;

namespace SmartRewardPayments{

typedef enum{
    NoError,
    DatabaseError,
    NotSynced,
    NoRewardBlock,
    InvalidRewardList
} Result;
SmartRewardPayments::Result ValidateRewardPayments(const CTransaction& txCoinbase, int nBlockHeight);
CSmartRewardSnapshotList GetPaymentsForBlock(const int nHeight, SmartRewardPayments::Result &result);
void FillRewardPayments(CMutableTransaction& txNew, int nBlockHeight, std::vector<CTxOut>& voutSuperblockRet);

}
#endif // REWARDSPAYMENTS_H
