// Copyright (c) 2018 - The SmartCash Developers
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

static std::pair<int,CSmartRewardSnapshotPtrList> *paymentData = nullptr;

static void ResetPaymentData()
{
    if( paymentData ){

        for( CSmartRewardSnapshot* s : paymentData->second ){
            delete s;
        }

        delete paymentData;
    }

    paymentData = new std::pair<int,CSmartRewardSnapshotPtrList>();
    paymentData->second.clear();
}

struct CompareRewardScore
{
    bool operator()(const std::pair<arith_uint256, CSmartRewardSnapshot*>& s1,
                    const std::pair<arith_uint256, CSmartRewardSnapshot*>& s2) const
    {
        return (s1.first != s2.first) ? (s1.first < s2.first) : (s1.second->balance < s2.second->balance);
    }
};

struct ComparePaymentPrtList
{
    bool operator()(const CSmartRewardSnapshot* p1,
                    const CSmartRewardSnapshot* p2) const
    {
        return *p1 < *p2;
    }
};

CSmartRewardSnapshotPtrList SmartRewardPayments::GetPayments(const CSmartRewardRound &round, const int64_t nPayoutDelay, const int nHeight, int64_t blockTime, SmartRewardPayments::Result &result)
{
    int64_t nPayeeCount = round.eligibleEntries - round.disqualifiedEntries;

    // If we have no eligible addresses. Just to make sure...wont happen.
    if( nPayeeCount <= 0 ){
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardSnapshotPtrList();
    }

    int nFirst_1_3_Round = MainNet() ? nRewardsFirst_1_3_Round : nRewardsFirst_1_3_Round_Testnet;

    int64_t nBlockPayees = round.nBlockPayees;
    int64_t nPayoutInterval = round.nBlockInterval;

    int64_t nRewardBlocks = nPayeeCount / nBlockPayees;
    // If we dont match nRewardPayoutsPerBlock add one more block for the remaining payments.
    if( nPayeeCount % nBlockPayees ) nRewardBlocks += 1;

    int64_t nLastRoundBlock = round.endBlockHeight + nPayoutDelay + ( (nRewardBlocks - 1) * nPayoutInterval );

    if( nHeight <= nLastRoundBlock && !(( nLastRoundBlock - nHeight ) % nPayoutInterval) ){
        // We have a reward block! Now try to create the payments vector.

        // Do this stuff only one time and leave it in memory until all blocks are paid
        if( !paymentData || paymentData->first != round.number ){

            ResetPaymentData();

            if( round.number < nFirst_1_3_Round ){

                if( !prewards->GetRewardPayouts( round.number, paymentData->second ) ||
                    paymentData->second.size() != static_cast<size_t>(nPayeeCount) ){
                    result = SmartRewardPayments::DatabaseError;
                    return CSmartRewardSnapshotPtrList();
                }

                // Sort it to make sure the slices are the same network wide.
                std::sort(paymentData->second.begin(), paymentData->second.end(), ComparePaymentPrtList() );

            }else{

                CSmartRewardSnapshotPtrList roundPayments;
                if( !prewards->GetRewardPayouts( round.number, roundPayments ) ||
                    roundPayments.size() != static_cast<size_t>(nPayeeCount) ){
                    result = SmartRewardPayments::DatabaseError;
                    return CSmartRewardSnapshotPtrList();
                }

                uint256 blockHash;
                if(!GetBlockHash(blockHash, round.startBlockHeight)) {
                    LogPrintf("SmartRewardPayments::GetPayments_1_3 -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", round.startBlockHeight);
                    result = SmartRewardPayments::CoreError;
                    return CSmartRewardSnapshotPtrList();
                }

                std::vector<std::pair<arith_uint256, CSmartRewardSnapshot*>> vecScores;
                // Since we use payouts stretched out over a week better to have some "random" sort here
                // based on a score calculated with the round start's blockhash.
                for (auto s : roundPayments) {
                    arith_uint256 nScore = s->CalculateScore(blockHash);
                    vecScores.push_back(std::make_pair(nScore,s));
                }

                std::sort(vecScores.begin(), vecScores.end(), CompareRewardScore());

                for(auto s : vecScores)
                    paymentData->second.push_back(s.second);
            }
            // And set the round number the payment data belongs to
            paymentData->first = round.number;
        }

        // Index of the current payout block for this round.
        int64_t nRewardBlock = nRewardBlocks - ( (nLastRoundBlock - nHeight) / nPayoutInterval );
        int64_t nFinalBlockPayees = nBlockPayees;

        // If the to be paid addresses are no multile of nRewardPayoutsPerBlock
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
        if( nEndIndex > paymentData->second.size() ){
            // Should not happen!
            result = SmartRewardPayments::DatabaseError;
            return CSmartRewardSnapshotPtrList();
        }

        // Finally return the subvector with the payees of this blockHeight!
        return CSmartRewardSnapshotPtrList(paymentData->second.begin() + nStartIndex, paymentData->second.begin() + nEndIndex);
    }

    // If we arent in any rounds payout range!
    result = SmartRewardPayments::NoRewardBlock;

    return CSmartRewardSnapshotPtrList();
}

CSmartRewardSnapshotPtrList SmartRewardPayments::GetPaymentsForBlock(const int nHeight, int64_t blockTime, SmartRewardPayments::Result &result)
{
    result = SmartRewardPayments::Valid;

    if(nHeight > sporkManager.GetSporkValue(SPORK_15_SMARTREWARDS_BLOCKS_ENABLED)) {
        LogPrint("smartrewards", "SmartRewardPayments::GetPaymentsForBlock -- Disabled");
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardSnapshotPtrList();
    }

    // If we are not yet at the 1.2 payout block time.
    if( ( MainNet() && nHeight < HF_V1_2_SMARTREWARD_HEIGHT + nRewardsBlocksPerRound_1_2 ) ||
        ( TestNet() && nHeight < nFirstRoundEndBlock_Testnet ) ){
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardSnapshotPtrList();
    }

    CSmartRewardRound round;

    {
        LOCK(cs_rewardrounds);
        round = prewards->GetLastRound();
    }

    // If there are no rounds yet or the database has an issue.
    if( !round.number ){
        result = SmartRewardPayments::NoRewardBlock;
        return CSmartRewardSnapshotPtrList();
    }

    // If the requested height is lower then the rounds end step forward to the
    // next round.
    int64_t nPayoutDelay = MainNet() ? nRewardPayoutStartDelay : nRewardPayoutStartDelay_Testnet;

    if( nHeight >= ( round.endBlockHeight + nPayoutDelay ) ){
        return SmartRewardPayments::GetPayments( round, nPayoutDelay, nHeight, blockTime, result );
    }

    // If we arent in any rounds payout range!
    result = SmartRewardPayments::NoRewardBlock;

    return CSmartRewardSnapshotPtrList();
}

void SmartRewardPayments::FillPayments(CMutableTransaction &coinbaseTx, int nHeight, int64_t prevBlockTime, std::vector<CTxOut>& voutSmartRewards)
{

    SmartRewardPayments::Result result;
    CSmartRewardSnapshotPtrList rewards =  SmartRewardPayments::GetPaymentsForBlock(nHeight, prevBlockTime, result);

    // only create rewardblocks if a rewardblock is actually required at the current height.
    if( result == SmartRewardPayments::Valid && rewards.size() ) {
            LogPrintf("FillRewardPayments -- triggered rewardblock creation at height %d with %d payees\n", nHeight, rewards.size());

            for( auto s : rewards )
            {
                if( s->reward > 0){
                    CTxOut out = CTxOut(s->reward, s->id.GetScript());
                    coinbaseTx.vout.push_back(out);
                    voutSmartRewards.push_back(out);
                }
            }
    }
}


SmartRewardPayments::Result SmartRewardPayments::Validate(const CBlock& block, int nHeight, CAmount &smartReward)
{
    SmartRewardPayments::Result result;

    smartReward = 0;

    const CTransaction &txCoinbase = block.vtx[0];

    CSmartRewardSnapshotPtrList rewards =  SmartRewardPayments::GetPaymentsForBlock(nHeight, block.GetBlockTime(), result);

    if( result == SmartRewardPayments::Valid && rewards.size() ) {

            LogPrintf("ValidateRewardPayments -- found rewardblock at height %d with %d payees\n", nHeight, rewards.size());

            for( auto payout : rewards )
            {
                if( payout->reward == 0 ) continue;

                // Search for the reward payment in the transactions outputs.
                auto isInOutputs = std::find_if(txCoinbase.vout.begin(), txCoinbase.vout.end(), [payout](const CTxOut &txout) -> bool {
                    return payout->id.GetScript() == txout.scriptPubKey && abs(payout->reward - txout.nValue) < 1000;
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
