// Copyright (c) 2018 - The SmartCash Developers
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
static const int64_t nRewardPayoutBlockInterval = 2;
//! Number of payouts per rewardblock
static const int64_t nRewardPayoutsPerBlock = 1000;

//! Testnet parameter
static const int64_t nRewardPayoutStartDelay_Testnet = 100;
static const int64_t nRewardPayoutsPerBlock_1_Testnet = 500;
static const int64_t nRewardPayoutBlockInterval_1_Testnet = 5;
static const int64_t nRewardPayoutsPerBlock_2_Testnet = 1000;
static const int64_t nRewardPayoutBlockInterval_2_Testnet = 2;

namespace SmartRewardPayments{

typedef enum{
    Valid,
    DatabaseError,
    NotSynced,
    NoRewardBlock,
    InvalidRewardList
} Result;

CSmartRewardSnapshotList GetPaymentsForBlock(const int nHeight, int64_t blockTime, SmartRewardPayments::Result &result);
SmartRewardPayments::Result Validate(const CBlock& block, const int nHeight, CAmount& smartReward);
void FillPayments(CMutableTransaction& txNew, int nHeight, int64_t prevBlockTime, std::vector<CTxOut>& voutSmartRewards);

}
#endif // REWARDSPAYMENTS_H
