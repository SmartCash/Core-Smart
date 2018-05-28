// Copyright (c) 2018 dustinface - SmartCash Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartrewards/rewards.h"
#include "smartrewards/rewardspayments.h"

#include "chainparams.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "hash.h"
#include "pow.h"
#include "ui_interface.h"
#include "init.h"

#include <stdint.h>

CSmartRewardSnapshotList SmartRewardPayments::GetPaymentsForBlock(const int nHeight, int64_t blockTime, SmartRewardPayments::Result &result)
{
    result = SmartRewardPayments::Valid;

    // If we are not yet at the 1.2 payout block time.
    if( ( MainNet() && nHeight < HF_V1_2_START_HEIGHT ) ||
        ( TestNet() && nHeight < nFirstRoundEndBlock_Testnet ) ){
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardSnapshotList();
    }

    CSmartRewardRound round;
    CSmartRewardSnapshotList roundPayments;

    {
        LOCK(cs_rewardrounds);
        round = prewards->GetLastRound();
    }

    // If there are no rounds yet or the database has an issue.
    if( !round.number ){
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardSnapshotList();
    }

    // If the requested height is lower then the rounds end step forward to the
    // next round.
    int64_t delay = MainNet() ? nRewardPayoutStartDelay : nRewardPayoutStartDelay_Testnet;

    if( nHeight >= ( round.endBlockHeight + delay ) ){

        size_t eligibleEntries = round.eligibleEntries - round.disqualifiedEntries;
        int rewardBlocks = eligibleEntries / nRewardPayoutsPerBlock;
        // If we dont match nRewardPayoutsPerBlock add one more block for the remaining payments.
        if( eligibleEntries % nRewardPayoutsPerBlock ) rewardBlocks += 1;

        int lastRoundBlock = round.endBlockHeight + delay + ( (rewardBlocks - 1) * nRewardPayoutBlockInterval );

        if( nHeight <= lastRoundBlock && !(( lastRoundBlock - nHeight ) % nRewardPayoutBlockInterval) ){
            // We have a reward block! Now try to create the payments vector.

            if( !prewards->GetRewardPayouts( round.number, roundPayments ) ||
                roundPayments.size() != eligibleEntries ){
                result = SmartRewardPayments::DatabaseError;
                return CSmartRewardSnapshotList();
            }

            // Sort it to make sure the slices are the same network wide.
            std::sort(roundPayments.begin(), roundPayments.end());

            // Index of the current payout block for this round.
            int rewardBlock = rewardBlocks - ( (lastRoundBlock - nHeight) / nRewardPayoutBlockInterval );
            int blockPayees = nRewardPayoutsPerBlock;

            // If the to be paid addresses are no multile of nRewardPayoutsPerBlock
            // the last payout block has less payees than the others.
            if( rewardBlock == rewardBlocks && eligibleEntries % nRewardPayoutsPerBlock ){
                // Use the remainders here..
                blockPayees = eligibleEntries % nRewardPayoutsPerBlock;
            }

            // As start index we want to use the current payout block index + payouts per block as offset.
            size_t startIndex = (rewardBlock - 1) * nRewardPayoutsPerBlock;
            // As ennd index we use the startIndex + number of payees for this round.
            size_t endIndex = startIndex + blockPayees;
            // If for any reason the calculations end up in an overflow of the vector return an error.
            if( endIndex > roundPayments.size() ){
                // Should not happen!
                result = SmartRewardPayments::DatabaseError;
                return CSmartRewardSnapshotList();
            }

            // Finally return the subvector with the payees of this blockHeight!
            return CSmartRewardSnapshotList(roundPayments.begin() + startIndex, roundPayments.begin() + endIndex);
        }
    }

    // If we arent in any rounds payout range!
    result = SmartRewardPayments::NoRewardBlock;

    return CSmartRewardSnapshotList();
}

void SmartRewardPayments::FillPayments(CMutableTransaction &coinbaseTx, int nHeight, int64_t prevBlockTime, std::vector<CTxOut>& voutSmartRewards)
{

    SmartRewardPayments::Result result;
    CSmartRewardSnapshotList rewards =  SmartRewardPayments::GetPaymentsForBlock(nHeight, prevBlockTime, result);

    // only create rewardblocks if a rewardblock is actually required at the current height.
    if( result == SmartRewardPayments::Valid && rewards.size() ) {
            LogPrintf("FillRewardPayments -- triggered rewardblock creation at height %d with %d payees\n", nHeight, rewards.size());

            BOOST_FOREACH(CSmartRewardSnapshot s, rewards)
            {
                CTxOut out = CTxOut(s.reward, s.id.GetScript());
                coinbaseTx.vout.push_back(out);
                voutSmartRewards.push_back(out);
            }
    }
}


SmartRewardPayments::Result SmartRewardPayments::Validate(const CBlock& block, int nHeight, CAmount &smartReward)
{
    SmartRewardPayments::Result result;

    smartReward = 0;

    const CTransaction &txCoinbase = block.vtx[0];

    CSmartRewardSnapshotList rewards =  SmartRewardPayments::GetPaymentsForBlock(nHeight, block.GetBlockTime(), result);

    if( result == SmartRewardPayments::Valid && rewards.size() ) {

            LogPrintf("ValidateRewardPayments -- found rewardblock at height %d with %d payees\n", nHeight, rewards.size());

            BOOST_FOREACH(const CSmartRewardSnapshot &payout, rewards)
            {

                // Search for the reward payment in the transactions outputs.
                auto isInOutputs = std::find_if(txCoinbase.vout.begin(), txCoinbase.vout.end(), [payout](const CTxOut &txout) -> bool {
                    return payout.id.GetScript() == txout.scriptPubKey && payout.reward == txout.nValue;
                });

                // If the payout is not in the list?
                if( isInOutputs == txCoinbase.vout.end() ){
                    LogPrintf("ValidateRewardPayments -- missing payment %s",payout.ToString() );
                    result = SmartRewardPayments::InvalidRewardList;
                    // We could return here..But lets print which payments else are missing.
                    // return result;
                }else{
                    smartReward += payout.reward;
                }
            }

    }else if( result == SmartRewardPayments::NotSynced || result == SmartRewardPayments::NoRewardBlock ){
        // If we are not synced yet, our database has any issue (should't happen), or the asked block
        // if no expected reward block just accept the block and let the rest of the network handle the reward validation.
        result = SmartRewardPayments::Valid;
    }

    return result;
}
