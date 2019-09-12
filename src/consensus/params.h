// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_CONSENSUS_PARAMS_H
#define SMARTCASH_CONSENSUS_PARAMS_H

#include "uint256.h"
#include <map>
#include <string>

namespace Consensus {

enum DeploymentPos
{
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_CSV, // Deployment of BIP68, BIP112, and BIP113.
    DEPLOYMENT_SEGWIT, // Deployment of BIP141, BIP143, and BIP147.
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;

    int nSmartnodePaymentsStartBlock;
    int nSmartnodePaymentsIncreaseBlock;
    int nSmartnodeMinimumConfirmations;
    int nInstantSendKeepLock; // in blocks

    int nProposalValidityVoteBlocks;
    int nProposalFundingVoteBlocks;
    int nVotingMinYesPercent; // Min percent of yes votes for a proposal to become funded
    int nVotingFilterElements;

    /** Used to check majorities for block version upgrade */
    int nMajorityEnforceBlockUpgrade;
    int nMajorityRejectBlockOutdated;
    int nMajorityWindow;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargetting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;

    /* SmartReward params */

    //! Number of blocks required before a block gets processed into the smartrewards db
    int nRewardsConfirmationsRequired;
    //! Number of blocks per round with 1.2 rules
    int nRewardsBlocksPerRound_1_2;
    //! Number of blocks per round with 1.3 rules
    int nRewardsBlocksPerRound_1_3;
    //! Number of the first round with 1.3 rules
    int nRewardsFirst_1_3_Round;
    //! Number of blocks to wait until we start to pay the rewards after a cycles end.
    int nRewardsPayoutStartDelay;
    //! Number of blocks to wait between reward payout blocks for 1.2 rounds
    int nRewardsPayouts_1_2_BlockInterval;
    //! Number of payouts per rewardblock for 1.2 rounds
    int nRewardsPayouts_1_2_BlockPayees;

    //! 1.3 Parameter
    int nRewardsPayouts_1_3_BlockStretch;
    int nRewardsPayouts_1_3_BlockPayees;

    std::string strRewardsGlobalVoteProofAddress;
};
} // namespace Consensus

#endif // SMARTCASH_CONSENSUS_PARAMS_H
