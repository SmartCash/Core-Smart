// Copyright (c) 2018 dustinface - SmartCash Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartmining/miningpayments.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"

#include "smartnode/smartnodeman.h"
#include "smartnode/smartnodepayments.h"
#include "smartrewards/rewardspayments.h"
#include "smarthive/hive.h"
#include "smarthive/hivepayments.h"

// Here is the place to later research with the possible mining adjustments.

CAmount GetMiningReward(CBlockIndex * pindex, CAmount blockReward)
{
    return blockReward / 20; // 5%
}

void SmartMining::FillPayment(CMutableTransaction& coinbaseTx, int nHeight, CBlockIndex * pindexPrev, CAmount blockReward)
{
    coinbaseTx.vout[0].nValue = GetMiningReward(pindexPrev, blockReward);
}

bool SmartMining::Validate(const CBlock &block, CBlockIndex *pindex, CValidationState& state, CAmount nFees)
{
    const CChainParams& chainparams = Params();
    CAmount coinbase = block.vtx[0].GetValueOut();
    CAmount blockReward = GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
    CAmount miningReward = GetMiningReward(pindex, blockReward);
    CAmount hiveReward = 0, nodeReward = 0, smartReward = 0;

    SmartHivePayments::Result result = SmartHivePayments::Validate(block.vtx[0],pindex->nHeight, pindex->GetBlockTime(), hiveReward);
    if( result != SmartHivePayments::Valid ){
        LogPrintf("SmartMining::Validate - Invalid hive payment %s\n", block.vtx[0].ToString());
        return state.DoS(100, false, SmartHivePayments::RejectionCode(result),
                                     SmartHivePayments::RejectionMessage(result));
    }

    if (!SmartNodePayments::IsPaymentValid(block.vtx[0], pindex->nHeight, blockReward, nodeReward)) {
        LogPrintf("SmartMining::Validate - Invalid node payment %s\n", block.vtx[0].ToString());
        return state.DoS(0, error("ConnectBlock(SMARTCASH): couldn't find smartnode payments"),
                                REJECT_INVALID, "bad-cb-payee");
    }

    if( SmartRewardPayments::Validate(block,pindex->nHeight, smartReward) != SmartRewardPayments::Valid ){
         LogPrintf("SmartMining::Validate - Invalid smartreward payment %s\n", block.vtx[0].ToString());
        return state.DoS(100, false, REJECT_INVALID_SMARTREWARD_PAYMENTS,
                     "CTransaction::CheckTransaction() : SmartReward payment list is invalid");
    }

    if( ( MainNet() && pindex->nHeight >= HF_V1_2_START_VALIDATION_HEIGHT && pindex->nHeight <= HF_CHAIN_REWARD_END_HEIGHT ) ||
        ( TestNet() )){
        if( coinbase > (nFees + nodeReward + hiveReward + smartReward + miningReward) ){
             LogPrintf("SmartMining::Validate - Coinbase too high! %s\n", block.vtx[0].ToString());
            return state.DoS(100, false, REJECT_INVALID,
                         "CTransaction::CheckTransaction() : Coinbase value too high");
        }
    }

    return true;
}
