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

CSmartRewardSnapshotList SmartRewardPayments::GetPaymentsForBlock(const int nHeight, SmartRewardPayments::Result &result)
{
    result = SmartRewardPayments::NoError;

    // If we are not yet at the 1.2 payout block height.
    if( nHeight < HF_V1_2_PAYMENTS_HEIGHT ){
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardSnapshotList();
    }

    // If the rewardlist is not synced yet we have no chance to create the payments.
    if( !prewards->IsSynced() ){
        result = SmartRewardPayments::NotSynced;
        return CSmartRewardSnapshotList();
    }

    CSmartRewardRoundList rounds;
    CSmartRewardSnapshotList roundPayments;
    // If there are no rounds yet or the database has an issue.
    if( !prewards->GetRewardRounds(rounds) ){
        result = SmartRewardPayments::DatabaseError;
        return CSmartRewardSnapshotList();
    }

    BOOST_FOREACH( const CSmartRewardRound &round, rounds)
    {
        // If the requested height is lower then the rounds end step forward to the
        // next round.
        if( nHeight < round.endBlockHeight + nRewardPayoutStartDelay ) continue;

        int rewardBlocks = round.eligibleEntries / nRewardPayoutsPerBlock;
        // If we dont match nRewardPayoutsPerBlock add one more block for the remaining payments.
        if(round.eligibleEntries % nRewardPayoutsPerBlock ) rewardBlocks += 1;

        int lastRoundBlock = round.endBlockHeight + nRewardPayoutStartDelay + ( rewardBlocks * nRewardPayoutBlockInterval );

        // If we are out of the payout range of this round.
        if( nHeight > lastRoundBlock ){
            result = SmartRewardPayments::NoRewardBlock;
            return CSmartRewardSnapshotList();
        }

        if( !(( lastRoundBlock - nHeight ) % nRewardPayoutBlockInterval) ){
            // We have a reward block! Now try to create the payments vector.

            if( prewards->GetRewardPayouts( round.number, roundPayments ) ){
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
            if( rewardBlock == rewardBlocks && round.eligibleEntries % nRewardPayoutsPerBlock ){
                // Use the remainders here..
                blockPayees = round.eligibleEntries % nRewardPayoutsPerBlock;
            }

            // As start index we want to use the current payout block index + payouts per block as offset.
            size_t startIndex = rewardBlock * nRewardPayoutsPerBlock;
            // As ennd index we use the startIndex + number of payees for this round. -1 since we start with 0.
            size_t endIndex = startIndex + blockPayees - 1;
            // If for any reason the calculations end up in an overflow of the vector return an error.
            if( startIndex + endIndex > roundPayments.size() || endIndex > roundPayments.size() ){
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

SmartRewardPayments::Result SmartRewardPayments::ValidateRewardPayments(const CTransaction& txCoinbase, int nBlockHeight)
{
    SmartRewardPayments::Result result;
    CSmartRewardSnapshotList rewards =  SmartRewardPayments::GetPaymentsForBlock(nBlockHeight, result);

    if( result == SmartRewardPayments::NoError && rewards.size() ) {

            LogPrint("rewardpayments","ValidateRewardPayments -- found rewardblock at height %d with %d payees\n", nBlockHeight, rewards.size());

            BOOST_FOREACH(const CSmartRewardSnapshot &payout, rewards)
            {

                // Search for the reward payment in the transactions outputs.
                auto isInOutputs = std::find_if(txCoinbase.vout.begin(), txCoinbase.vout.end(), [payout](const CTxOut &txout) -> bool {
                    return payout.id.GetScript() == txout.scriptPubKey && payout.reward == txout.nValue;
                });

                // If the payout is not in the list?
                if( isInOutputs == txCoinbase.vout.end() ){
                    LogPrint("rewardpayments", "ValidateRewardPayments -- missing payment %s",payout.ToString() );
                    result = SmartRewardPayments::InvalidRewardList;
                    // We could return here..But lets print which payments else are missing.
                    // return result;
                }
            }

    }else if( result == SmartRewardPayments::NotSynced || result == SmartRewardPayments::DatabaseError ){
        // If we are not synced yet, our database has any issue (should't happen), or the asked block
        // if no expected reward block just accept the block and let the rest of the network handle the reward validation.
        result = SmartRewardPayments::NoError;
    }

    return result;
}


void SmartRewardPayments::FillRewardPayments(CMutableTransaction &coinbaseTx, int nBlockHeight, std::vector<CTxOut> &voutSuperblockRet)
{

    SmartRewardPayments::Result result;
    CSmartRewardSnapshotList rewards =  SmartRewardPayments::GetPaymentsForBlock(nBlockHeight, result);

    // only create rewardblocks if a rewardblock is actually required at the current height.
    if( result == SmartRewardPayments::NoError && rewards.size() ) {
            LogPrint("rewardpayments", "FillRewardPayments -- triggered rewardblock creation at height %d with %d payees\n", nBlockHeight, rewards.size());

            BOOST_FOREACH(CSmartRewardSnapshot s, rewards)
            {
                CTxOut out = CTxOut(s.reward, s.id.GetScript());
                coinbaseTx.vout.push_back(out);
                voutSuperblockRet.push_back(out);
            }
    }
}
