// Copyright (c) 2017 - 2019 - The SmartCash Developers
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
#include "smartnode/spork.h"

#include <stdint.h>

CSmartRewardResultEntryPtrList SmartRewardPayments::GetPayments(const CSmartRewardsRoundResult *pResult, const int64_t nPayoutDelay, const int nHeight, int64_t blockTime, SmartRewardPayments::Result &result)
{
    int64_t nPayeeCount = pResult->round.eligibleEntries - pResult->round.disqualifiedEntries;

    // If we have no eligible addresses. Just to make sure...wont happen.
    if( nPayeeCount <= 0 ){
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardResultEntryPtrList();
    }

    int64_t nBlockPayees = pResult->round.nBlockPayees;
    int64_t nPayoutInterval = pResult->round.nBlockInterval;

    int64_t nRewardBlocks = nPayeeCount / nBlockPayees;
    // If we dont match nRewardsPayoutsPerBlock add one more block for the remaining payments.
    if( nPayeeCount % nBlockPayees ) nRewardBlocks += 1;

    int64_t nLastRoundBlock = pResult->round.endBlockHeight + nPayoutDelay + ( (nRewardBlocks - 1) * nPayoutInterval );

    if( nHeight <= nLastRoundBlock && !(( nLastRoundBlock - nHeight ) % nPayoutInterval) ){
        // We have a reward block! Now try to create the payments vector.

        // Index of the current payout block for this round.
        int64_t nRewardBlock = nRewardBlocks - ( (nLastRoundBlock - nHeight) / nPayoutInterval );
        int64_t nFinalBlockPayees = nBlockPayees;

        // If the to be paid addresses are no multile of nRewardsPayoutsPerBlock
        // the last payout block has less payees than the others.
        if( nRewardBlock == nRewardBlocks && nPayeeCount % nBlockPayees ){
            // Use the remainders here..
            nFinalBlockPayees = nPayeeCount % nBlockPayees;
        }

        // As start index we want to use the current payout block index + payouts per block as offset.
        size_t nStartIndex = (nRewardBlock - 1) * nBlockPayees;
        // As ennd index we use the startIndex + number of payees for this round.
        size_t nEndIndex = nStartIndex + nFinalBlockPayees;
        // If for any reason the calculations end up in an overflow of the vector return an error.
        if( nEndIndex > pResult->payouts.size() ){
            // Should not happen!
            result = SmartRewardPayments::DatabaseError;
            return CSmartRewardResultEntryPtrList();
        }

        // Finally return the subvector with the payees of this blockHeight!
        return CSmartRewardResultEntryPtrList(pResult->payouts.begin() + nStartIndex, pResult->payouts.begin() + nEndIndex);
    }

    // If we arent in any rounds payout range!
    result = SmartRewardPayments::NoRewardBlock;

    return CSmartRewardResultEntryPtrList();
}

CSmartRewardResultEntryPtrList SmartRewardPayments::GetPaymentsForBlock(const int nHeight, int64_t blockTime, SmartRewardPayments::Result &result)
{
    result = SmartRewardPayments::Valid;

    if(nHeight > sporkManager.GetSporkValue(SPORK_15_SMARTREWARDS_BLOCKS_ENABLED)) {
        LogPrint("smartrewards", "SmartRewardPayments::GetPaymentsForBlock -- Disabled");
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardResultEntryPtrList();
    }

    // If we are not yet at the 1.2 payout block time.
    if( ( MainNet() && nHeight < HF_V1_2_SMARTREWARD_HEIGHT + Params().GetConsensus().nRewardsBlocksPerRound_1_2 ) ||
        ( TestNet() && nHeight < nFirstRoundEndBlock_Testnet ) ){
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardResultEntryPtrList();
    }

    const CSmartRewardsRoundResult *pResult = prewards->GetLastRoundResult();

    // If there are no rounds yet or the database has an issue.
    if( !pResult ){
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardResultEntryPtrList();
    }

    // If the requested height is lower then the rounds end step forward to the
    // next round.
    int64_t nPayoutDelay = Params().GetConsensus().nRewardsPayoutStartDelay;

    if( nHeight >= ( pResult->round.endBlockHeight + nPayoutDelay ) ){
        return SmartRewardPayments::GetPayments( pResult, nPayoutDelay, nHeight, blockTime, result );
    }

    // If we arent in any rounds payout range!
    result = SmartRewardPayments::NoRewardBlock;

    return CSmartRewardResultEntryPtrList();
}

void SmartRewardPayments::FillPayments(CMutableTransaction &coinbaseTx, int nHeight, int64_t prevBlockTime, std::vector<CTxOut>& voutSmartRewards)
{
    LOCK(cs_rewardscache);

    SmartRewardPayments::Result result;
    CSmartRewardResultEntryPtrList rewards =  SmartRewardPayments::GetPaymentsForBlock(nHeight, prevBlockTime, result);

    // only create rewardblocks if a rewardblock is actually required at the current height.
    if( result == SmartRewardPayments::Valid && rewards.size() ) {
            LogPrintf("FillRewardPayments -- triggered rewardblock creation at height %d with %d payees\n", nHeight, rewards.size());

            for( auto s : rewards )
            {
                if( s->reward > 0){
                    CTxOut out = CTxOut(s->reward, s->entry.id.GetScript());
                    coinbaseTx.vout.push_back(out);
                    voutSmartRewards.push_back(out);
                }
            }
    }
}


SmartRewardPayments::Result SmartRewardPayments::Validate(const CBlock& block, int nHeight, CAmount &smartReward)
{
    LOCK(cs_rewardscache);

    SmartRewardPayments::Result result;

    smartReward = 0;

    const CTransaction &txCoinbase = block.vtx[0];

    CSmartRewardResultEntryPtrList rewards =  SmartRewardPayments::GetPaymentsForBlock(nHeight, block.GetBlockTime(), result);

    if( result == SmartRewardPayments::Valid && rewards.size() ) {

            LogPrintf("ValidateRewardPayments -- found rewardblock at height %d with %d payees\n", nHeight, rewards.size());

            for( auto payout : rewards )
            {
                if( payout->reward == 0 ) continue;

                // Search for the reward payment in the transactions outputs.
                auto isInOutputs = std::find_if(txCoinbase.vout.begin(), txCoinbase.vout.end(), [payout](const CTxOut &txout) -> bool {
                    return payout->entry.id.GetScript() == txout.scriptPubKey && abs(payout->reward - txout.nValue) < 1000;
                });

                // If the payout is not in the list?
                if( isInOutputs == txCoinbase.vout.end() ){
                    LogPrintf("ValidateRewardPayments -- missing payment %s",payout->ToString() );
                    result = SmartRewardPayments::InvalidRewardList;
                    // We could return here..But lets print which payments else are missing.
                    // return result;
                }else{
                    smartReward += isInOutputs->nValue;
                }
            }

    }else if( result == SmartRewardPayments::NotSynced || result == SmartRewardPayments::NoRewardBlock ){
        // If we are not synced yet, our database has any issue (should't happen), or the asked block
        // if no expected reward block just accept the block and let the rest of the network handle the reward validation.
        result = SmartRewardPayments::Valid;
    }

    return result;
}
