// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_CONSENSUS_CONSENSUS_H
#define SMARTCASH_CONSENSUS_CONSENSUS_H

#include <stdint.h>

/** The maximum allowed size for a serialized block, in bytes (only for buffer size limits) */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 2000000;
/** The maximum allowed weight for a block, see BIP 141 (network rule) */
static const unsigned int MAX_BLOCK_WEIGHT = 4000000;
/** The maximum allowed size for a block excluding witness data, in bytes (network rule) */
static const unsigned int MAX_BLOCK_BASE_SIZE = 1000000;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const int64_t MAX_BLOCK_SIGOPS_COST = 160000;
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;
/** Version 1.0 net start block*/
static const int HF_V1_0_START_HEIGHT = 90000;
/** Smartnode start block*/
static const int HF_V1_1_SMARTNODE_HEIGHT = 300000;
///** v1.2 mining changes start block*/
// DEPRECATED, we use the time for this fork to make sure we end up on the
// SmartRewards deadline.
//static const int HF_V1_2_PAYMENTS_HEIGHT = 550000;
/** SmartRewards automation start time*/
static const int HF_V1_2_LEGACY_SMARTREWARD_END_TIME = 1532502000; // 07/25/2018 @ 7:00am (UTC)
/** SmartCash max reward block */
static const int HF_CHAIN_REWARD_END_HEIGHT = 717499999;

/** Testnets 1.2 mining changes start block*/
static const int TESTNET_V1_2_TIMED_MINING_HEIGHT = 20;
/** Testnets 1.2 payment start block*/
static const int TESTNET_V1_2_PAYMENTS_HEIGHT = 1000;
/** Testnets 1.2 SmartRewards automation start block*/
static const int TESTNET_V1_2_LEGACY_SMARTREWARD_END_TIME = 1527231600; // 05/25/2018 @ 7:00am (UTC)


inline unsigned int MaxBlockSigOps()
{
    return MAX_BLOCK_SERIALIZED_SIZE / 50;
}

/** Flags for nSequence and nLockTime locks */
enum {
    /* Interpret sequence numbers as relative lock-time constraints. */
    LOCKTIME_VERIFY_SEQUENCE = (1 << 0),

    /* Use GetMedianTimePast() instead of nTime for end point timestamp. */
    LOCKTIME_MEDIAN_TIME_PAST = (1 << 1),
};

#endif // SMARTCASH_CONSENSUS_CONSENSUS_H
