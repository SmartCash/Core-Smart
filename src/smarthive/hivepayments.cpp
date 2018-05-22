// Copyright (c) 2018 dustinface - SmartCash Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smarthive/hivepayments.h"
#include "smarthive/hive.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "validation.h"

static CSmartHiveSplit *hiveSplitInitial = nullptr;
static CSmartHiveSplit *hiveSplit_1_0 = nullptr;
static CSmartHiveSplit *hiveSplit_1_1 = nullptr;
static CSmartHiveSplit *hiveSplit_1_2 = nullptr;
static CSmartHiveSplit *hiveSplitDisabled = nullptr;

void SmartHivePayments::Init()
{
    static bool init = false;
    if( init ) return;

    hiveSplitInitial = new CSmartHiveClassicSplit {
        95, // Split 95% of the block reward as followed.
        {
            new CSmartHiveClassic(SmartHive::Outreach,  0.08),
            new CSmartHiveClassic(SmartHive::Support,  0.08),
            new CSmartHiveClassic(SmartHive::Development,  0.08),
            new CSmartHiveClassic(SmartHive::SmartRewards,  0.15),
            new CSmartHiveClassic(SmartHive::ProjectTreasury,  0.56)
        }
    };

    hiveSplit_1_0 = new CSmartHiveRotationSplit(
        95, // Split 95% of the block reward as followed.
        {
            new CSmartHiveRotation(SmartHive::Outreach, 0,7),
            new CSmartHiveRotation(SmartHive::Support, 8,15),
            new CSmartHiveRotation(SmartHive::Development, 16,23),
            new CSmartHiveRotation(SmartHive::SmartRewards, 24,38),
            new CSmartHiveRotation(SmartHive::ProjectTreasury, 39,94)
        }
    );

    hiveSplit_1_1 = new CSmartHiveRotationSplit(
        85, // Split 85% of the block reward as followed.
        {
            new CSmartHiveRotation(SmartHive::Outreach, 0,7),
            new CSmartHiveRotation(SmartHive::Support, 8,15),
            new CSmartHiveRotation(SmartHive::Development, 16,23),
            new CSmartHiveRotation(SmartHive::SmartRewards, 24,38),
            new CSmartHiveRotation(SmartHive::ProjectTreasury, 39,84)
        }
    );

    hiveSplit_1_2 = new CSmartHiveRotationSplit(
        70, // Split 70% of the block reward as followed.
        {
            new CSmartHiveRotation(SmartHive::Outreach, 0,3),
            new CSmartHiveRotation(SmartHive::Support, 4,7),
            new CSmartHiveRotation(SmartHive::Development, 8,11),
            new CSmartHiveRotation(SmartHive::Outreach2, 12,15),
            new CSmartHiveRotation(SmartHive::Web, 16,19),
            new CSmartHiveRotation(SmartHive::Quality, 20,23),
            new CSmartHiveRotation(SmartHive::ProjectTreasury, 24,69),
        }
    );

    hiveSplitDisabled = new CSmartHiveSplitDisabled();

    init = true;
}

const CSmartHiveSplit * GetHiveSplit(int nHeight, int64_t blockTime)
{
    if ((MainNet() && (nHeight < HF_V1_0_START_HEIGHT)) || (TestNet() && (nHeight < TESTNET_V1_2_PAYMENTS_HEIGHT))) {
        return hiveSplitInitial;
    }else if ((nHeight >= HF_V1_0_START_HEIGHT) && (nHeight < HF_V1_1_SMARTNODE_HEIGHT)) {
        // We have a lot blocks with missing hive payments in this range. Just accept them.
        if( MainNet() && nHeight >= 227898 && nHeight <= 259345) return hiveSplitDisabled;
        // Out of this range use the v1.0 split.
        return hiveSplit_1_0;
    }else if ((nHeight >= HF_V1_1_SMARTNODE_HEIGHT) && (blockTime < HF_V1_2_LEGACY_SMARTREWARD_END_TIME)) {
        return hiveSplit_1_1;
    }else if (((MainNet() && (blockTime >= TESTNET_V1_2_LEGACY_SMARTREWARD_END_TIME)) || (TestNet() && (nHeight >= TESTNET_V1_2_PAYMENTS_HEIGHT))) && (nHeight <= HF_CHAIN_REWARD_END_HEIGHT)) {
        return hiveSplit_1_2;
    }else if(nHeight <= HF_CHAIN_REWARD_END_HEIGHT){
        return hiveSplitDisabled;
    }

    return nullptr;
}

SmartHivePayments::Result SmartHivePayments::Validate(const CTransaction& txCoinbase, int nHeight, int64_t blockTime)
{
    // No validtion required for the first block or after the change to fee only.
    if ( nHeight <= 1 || nHeight > HF_CHAIN_REWARD_END_HEIGHT) return SmartHivePayments::Valid;

    CAmount blockReward = GetBlockValue(nHeight, 0, blockTime);

// Not longer needed but stays as comment.
//    if(fMainNet && GetAdjustedTime() <= nStartRewardTime){
//            return state.DoS(100, error("CTransaction::CheckTransaction() : transaction is too early"));
//    }

    // From 1.0 we have the block rotation.
    const CSmartHiveSplit * ptrHiveSplit = GetHiveSplit(nHeight, blockTime);
    // If the network changed to fee only.
    if( ptrHiveSplit == nullptr ) return SmartHivePayments::InvalidBlockHeight;
    // If there is no hive payment in the coinbase.
    if( !ptrHiveSplit->Valididate(txCoinbase.vout,nHeight,blockReward)) return SmartHivePayments::HiveAddressMissing;

    // There we go! Correct hive payments found..
    return SmartHivePayments::Valid;
}

void SmartHivePayments::FillPayments(CMutableTransaction& txNew, int nHeight, int64_t blockTime, CAmount blockReward, std::vector<CTxOut>& voutSmartHives)
{
    // No payment required for the first block or after the change to fee only.
    if ( nHeight == 0 || nHeight > HF_CHAIN_REWARD_END_HEIGHT) return;

    const CSmartHiveSplit * ptrHiveSplit = GetHiveSplit(nHeight, blockTime);
    // If the network changed to fee only step out.
    if( ptrHiveSplit == nullptr ) return;
    // Fill the hive payments.
    ptrHiveSplit->FillPayment(txNew.vout, nHeight, blockReward, voutSmartHives);

}

bool CSmartHiveClassicSplit::Valididate(const std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward) const
{
    // We have no more hive payouts in fee only mode.
    if( !hives.size() ) return true;

    size_t found = 0;

    BOOST_FOREACH(const CTxOut& output, outputs){
    BOOST_FOREACH(CSmartHiveRewardBase *hive, hives){

            if( hive->GetScript() != output.scriptPubKey ) continue;
            if( abs( output.nValue - ( blockReward * hive->GetRatio() ) ) >= 2) continue;

            // We found a valid hive payment here!
            ++found;
            break;
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

bool CSmartHiveRotationSplit::Valididate(const std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward) const
{
    // We have no more hive payouts in fee only mode.
    if( !hives.size() ) return true;

    CAmount hiveReward = blockReward * percent;
    int rotation = nHeight - allocation * (nHeight/allocation);

    CSmartHiveRotation * ptrHive;

    BOOST_FOREACH(const CTxOut& output, outputs){
    BOOST_FOREACH(CSmartHiveRewardBase *hive, hives){

        ptrHive = static_cast<CSmartHiveRotation*> (hive);

        if( rotation < ptrHive->start || rotation > ptrHive->end) continue;
        if( ptrHive->GetScript() != output.scriptPubKey ) continue;
        if( abs( output.nValue - hiveReward ) >= 2) continue;

        // We found a valid hive payment here!
        return true;
    }}

    return false;
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


