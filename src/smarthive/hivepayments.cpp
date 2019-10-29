// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smarthive/hivepayments.h"
#include "smarthive/hive.h"
#include "smartnode/spork.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "validation.h"

static CSmartHiveSplit *hiveSplitInitial = nullptr;
static CSmartHiveSplit *hiveSplit_1_0 = nullptr;
static CSmartHiveSplit *hiveSplit_1_1 = nullptr;
static CSmartHiveSplit *hiveSplit_1_2 = nullptr;
static CSmartHiveSplit *hiveSplit_1_3 = nullptr;
static CSmartHiveSplit *hiveSplitDisabled = nullptr;
static CSmartHiveSplit *hiveSplitInvalid_1_0 = nullptr;

static int nPayoutInterval_1_2;
static int nPayoutInterval_1_3;

void SmartHivePayments::Init()
{
    static bool init = false;
    if( init ) return;

    hiveSplitInitial = new CSmartHiveClassicSplit {
        95, // Split 95% of the block reward as followed.
        {
            new CSmartHiveClassic(SmartHive::Outreach_Legacy,  0.08),
            new CSmartHiveClassic(SmartHive::Support_Legacy,  0.08),
            new CSmartHiveClassic(SmartHive::Development_Legacy,  0.08),
            new CSmartHiveClassic(SmartHive::SmartRewards_Legacy,  0.15),
            new CSmartHiveClassic(SmartHive::ProjectTreasury_Legacy,  0.56)
        }
    };

    hiveSplit_1_0 = new CSmartHiveRotationSplit(
        95, // Split 95% of the block reward as followed.
        {
            new CSmartHiveRotation(SmartHive::Outreach_Legacy, 0,7),
            new CSmartHiveRotation(SmartHive::Support_Legacy, 8,15),
            new CSmartHiveRotation(SmartHive::Development_Legacy, 16,23),
            new CSmartHiveRotation(SmartHive::SmartRewards_Legacy, 24,38),
            new CSmartHiveRotation(SmartHive::ProjectTreasury_Legacy, 39,94)
        }
    );

    hiveSplit_1_1 = new CSmartHiveRotationSplit(
        85, // Split 85% of the block reward as followed.
        {
            new CSmartHiveRotation(SmartHive::Outreach_Legacy, 0,7),
            new CSmartHiveRotation(SmartHive::Support_Legacy, 8,15),
            new CSmartHiveRotation(SmartHive::Development_Legacy, 16,23),
            new CSmartHiveRotation(SmartHive::SmartRewards_Legacy, 24,38),
            new CSmartHiveRotation(SmartHive::ProjectTreasury_Legacy, 39,84)
        }
    );

    if(MainNet()) nPayoutInterval_1_2 = 1000;
    else          nPayoutInterval_1_2 = 25;

    if(MainNet()) nPayoutInterval_1_3 = 10000;
    else          nPayoutInterval_1_3 = 50;

    hiveSplit_1_2 = new CSmartHiveBatchSplit(
        70, // Split 70% of the block reward as followed.
        nPayoutInterval_1_2, // Trigger the payouts every n blocks
        {
            new CSmartHiveClassic(SmartHive::Outreach_Legacy, 0.04),
            new CSmartHiveClassic(SmartHive::Support_Legacy, 0.04),
            new CSmartHiveClassic(SmartHive::Development_Legacy, 0.04),
            new CSmartHiveClassic(SmartHive::Outreach2_Legacy, 0.04),
            new CSmartHiveClassic(SmartHive::Web_Legacy, 0.04),
            new CSmartHiveClassic(SmartHive::Quality_Legacy, 0.04),
            new CSmartHiveClassic(SmartHive::ProjectTreasury_Legacy, 0.46),
        }
    );

    hiveSplit_1_3 = new CSmartHiveBatchSplit(
        55, // Split 55% of the block reward as followed.
        nPayoutInterval_1_3, // Trigger the payouts every n blocks
        {
            new CSmartHiveClassic(SmartHive::Exchanges, 0.05),
            new CSmartHiveClassic(SmartHive::Merchants, 0.05),
            new CSmartHiveClassic(SmartHive::Outreach, 0.0625),
            new CSmartHiveClassic(SmartHive::Support, 0.0625),
            new CSmartHiveClassic(SmartHive::Development, 0.0625),
            new CSmartHiveClassic(SmartHive::WebMobileSmartCard, 0.0625),
            new CSmartHiveClassic(SmartHive::ProjectTreasury, 0.2),
        }
    );

    hiveSplitDisabled = new CSmartHiveSplitDisabled();
    hiveSplitInvalid_1_0 = new CSmartHiveSplitInvalid(0.95);

    init = true;
}

const CSmartHiveSplit * GetHiveSplit(int nHeight, int64_t blockTime)
{

    if( MainNet() ){

        if ( nHeight > 1 && nHeight < HF_V1_0_START_HEIGHT ) {
            return hiveSplitInitial;
        }else if ( nHeight >= HF_V1_0_START_HEIGHT && nHeight < HF_V1_1_SMARTNODE_HEIGHT ) {
            // We have a lot blocks with missing hive payments in this range. Just accept them.
            if( nHeight >= 227000 && nHeight <= 259345) return hiveSplitInvalid_1_0;
            // Out of this range use the v1.0 split.
            return hiveSplit_1_0;
        }else if ( nHeight >= HF_V1_1_SMARTNODE_HEIGHT && nHeight < HF_V1_2_SMARTREWARD_HEIGHT ) {
            return hiveSplit_1_1;
        }else if ( (nHeight >= HF_V1_2_SMARTREWARD_HEIGHT) && nHeight < HF_V1_3_HEIGHT ) {
            return hiveSplit_1_2;
        }else if ( (nHeight >= HF_V1_3_HEIGHT) && nHeight < HF_CHAIN_REWARD_END_HEIGHT ) {
            return hiveSplit_1_3;
        }else if(nHeight <= 1 || nHeight >= HF_CHAIN_REWARD_END_HEIGHT){
            return hiveSplitDisabled;
        }

    }else{

        if ( nHeight < TESTNET_V1_2_PAYMENTS_HEIGHT ) {
            return hiveSplit_1_1;
        }else if ( nHeight >= TESTNET_V1_2_PAYMENTS_HEIGHT && nHeight < TESTNET_V1_3_HEIGHT ) {
            return hiveSplit_1_2;
        }else if ( nHeight >= TESTNET_V1_3_HEIGHT && nHeight < HF_CHAIN_REWARD_END_HEIGHT ) {
            return hiveSplit_1_3;
        }else if(nHeight >= HF_CHAIN_REWARD_END_HEIGHT){
            return hiveSplitDisabled;
        }

    }

    return nullptr;
}

SmartHivePayments::Result SmartHivePayments::Validate(const CTransaction& txCoinbase, int nHeight, int64_t blockTime, CAmount& hiveReward)
{

    CAmount blockReward = GetBlockValue(nHeight, 0, blockTime);

// Not longer needed but stays as comment.
//    if(fMainNet && GetAdjustedTime() <= nStartRewardTime){
//            return state.DoS(100, error("CTransaction::CheckTransaction() : transaction is too early"));
//    }

    const CSmartHiveSplit * ptrHiveSplit = GetHiveSplit(nHeight, blockTime);
    // If we got an invalid height. Should not happen.
    if( ptrHiveSplit == nullptr ) return SmartHivePayments::InvalidBlockHeight;
    // If there is no hive payment in the coinbase.
    if( !ptrHiveSplit->Valididate(txCoinbase.vout,nHeight,blockReward, hiveReward)) return SmartHivePayments::HiveAddressMissing;

    // There we go! Correct hive payments found..
    return SmartHivePayments::Valid;
}

void SmartHivePayments::FillPayments(CMutableTransaction& txNew, int nHeight, int64_t blockTime, CAmount blockReward, std::vector<CTxOut>& voutSmartHives)
{
    const CSmartHiveSplit * ptrHiveSplit = GetHiveSplit(nHeight, blockTime);
    // If we got an invalid height. Should not happen.
    if( ptrHiveSplit == nullptr ) return;
    // Fill the hive payments.
    ptrHiveSplit->FillPayment(txNew.vout, nHeight, blockReward, voutSmartHives);

}

bool CSmartHiveClassicSplit::Valididate(const std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward, CAmount& hiveReward) const
{
    size_t found = 0;

    hiveReward = 0;

    BOOST_FOREACH(const CTxOut& output, outputs){
    BOOST_FOREACH(CSmartHiveRewardBase *hive, hives){

            if( hive->GetScript() != output.scriptPubKey ) continue;
            if( abs( output.nValue - ( blockReward * hive->GetRatio() ) ) >= 2) continue;

            hiveReward += output.nValue;

            // We found a valid hive payment here!
            ++found;
            break; // Break the inner loop.
    }}

    return hives.size() == found;
}

void CSmartHiveClassicSplit::FillPayment(std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartHives) const
{
    CSmartHiveSplit::FillPayment(outputs,nHeight,blockReward,voutSmartHives);

    CAmount calcReward;

    BOOST_FOREACH(CSmartHiveRewardBase *hive, hives){
        calcReward = (CAmount)(blockReward * hive->GetRatio());
        voutSmartHives.push_back(CTxOut(calcReward, hive->GetScript()));
        outputs.push_back(CTxOut(calcReward, hive->GetScript()));
    }
}

void CSmartHiveRotationSplit::FillPayment(std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartHives) const
{
    CSmartHiveSplit::FillPayment(outputs,nHeight,blockReward,voutSmartHives);

    int rotation = nHeight - allocation * (nHeight/allocation);

    CSmartHiveRotation * ptrHive;

    BOOST_FOREACH(CSmartHiveRewardBase *hive, hives){

        ptrHive = static_cast<CSmartHiveRotation*> (hive);

        if( rotation < ptrHive->start || rotation > ptrHive->end) continue;

        // We found a valid hive payment here!
        CAmount hiveReward = (CAmount)(blockReward * percent);
        CTxOut out = CTxOut(hiveReward, ptrHive->GetScript());
        outputs.push_back(out);
        voutSmartHives.push_back(out);
        return;
    }
}

bool CSmartHiveRotationSplit::Valididate(const std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward, CAmount& hiveReward) const
{
    // We have no more hive payouts in fee only mode.
    if( !hives.size() ) return true;

    CAmount expected = blockReward * percent;
    int rotation = nHeight - allocation * (nHeight/allocation);

    CSmartHiveRotation * ptrHive;

    BOOST_FOREACH(CSmartHiveRewardBase *hive, hives){
    BOOST_FOREACH(const CTxOut& output, outputs){

        ptrHive = static_cast<CSmartHiveRotation*> (hive);

        if( rotation < ptrHive->start || rotation > ptrHive->end) continue;
        if( ptrHive->GetScript() != output.scriptPubKey ) continue;
        if( abs( output.nValue - expected ) >= 2) continue;

        hiveReward = output.nValue;

        // We found a valid hive payment here!
        return true;
    }}

    return false;
}

CAmount CSmartHiveBatchSplit::GetBatchReward(int nHeight) const
{
    CAmount reward = 0;
    int block = nHeight - trigger - 1;
    while (++block < nHeight) {
        reward += GetBlockValue(block, 0, INT_MAX);
    }

    return reward;
}

void CSmartHiveBatchSplit::FillPayment(std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward, std::vector<CTxOut> &voutSmartHives) const
{
    // Only add the payouts each "trigger" blocks
    if( (nHeight % trigger) ) return;

    CAmount batchReward = GetBatchReward(nHeight);

    BOOST_FOREACH(CSmartHiveRewardBase *hive, hives)
    {
        CAmount hiveReward = (CAmount)(batchReward * hive->GetRatio() );
        CTxOut out = CTxOut(hiveReward, hive->GetScript());
        outputs.push_back(out);
        voutSmartHives.push_back(out);
    }

}

bool CSmartHiveBatchSplit::Valididate(const std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward, CAmount& hiveReward) const
{
    hiveReward = 0;

    // Only validate the payouts each "trigger" blocks
    if( (nHeight % trigger) ) return true;

    size_t found = 0;
    CAmount batchReward = GetBatchReward(nHeight);

    BOOST_FOREACH(CSmartHiveRewardBase *hive, hives){
    BOOST_FOREACH(const CTxOut& output, outputs){

            if( hive->GetScript() != output.scriptPubKey ) continue;
            if( abs( output.nValue - ( batchReward * hive->GetRatio() ) ) >= 2) continue;

            hiveReward += output.nValue;

            // We found a valid hive payment here!
            ++found;
            break; // Break the inner loop.
    }}

    return hives.size() == found;
}

int SmartHivePayments::RejectionCode(SmartHivePayments::Result result)
{
    switch(result){
    case SmartHivePayments::TransactionTooEarly: return REJECT_TRANSACTION_TOO_EARLY;
    case SmartHivePayments::InvalidBlockHeight: return REJECT_TRANSACTION_TOO_EARLY;
    case SmartHivePayments::HiveAddressMissing: return REJECT_FOUNDER_REWARD_MISSING;
    default: return REJECT_INVALID;
    }
}

string SmartHivePayments::RejectionMessage(SmartHivePayments::Result result)
{
    switch(result){
    case SmartHivePayments::TransactionTooEarly: return "SmartHivePayments::RejectionMessage(TransactionTooEarly)";
    case SmartHivePayments::InvalidBlockHeight: return "SmartHivePayments::RejectionMessage(InvalidBlockHeight)";
    case SmartHivePayments::HiveAddressMissing: return "SmartHivePayments::RejectionMessage(HiveAddressMissing)";
    default: return "SmartHivePayments::RejectionMessage(UnknownReason)";
    }
}
