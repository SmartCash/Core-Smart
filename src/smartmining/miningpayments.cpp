// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartmining/miningpayments.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "messagesigner.h"

#include "smartnode/smartnodeman.h"
#include "smartnode/smartnodepayments.h"
#include "smartrewards/rewardspayments.h"
#include "smarthive/hive.h"
#include "smarthive/hivepayments.h"

// Here is the place to later research with the possible mining adjustments.

static std::map<uint8_t, CSmartAddress> mapMiningKeysMainnet = {

};

static std::map<uint8_t, CSmartAddress> mapMiningKeysTestnet = {
    {0,  CSmartAddress("TUcdknEDtqM5cRf6YFM5xRNzcAbQuNpJoA")},
    {1,  CSmartAddress("TGwRnVCEBouA75mkfgwZ5XGH66sjXJj2iq")},
    {2,  CSmartAddress("TYkeHMSS3VBfnH8i9yRqxnR3uxjavrSpoQ")},
    {63, CSmartAddress("TFDgrpTFGUL9NZgEjTMxuF5v6pw2tKSuRu")},
};

/**
  This is a workaroud to avoid hashrate attacks from bad pools until we have a proper solution.
  It allows us to force pools to sign the blocks with a private key which a pool can receive from us.
*/
static bool CheckSignature(const CBlock &block, CBlockIndex *pindex)
{

    // We dont check the signatures in the litemode, just accept the longest chain.
    if( fLiteMode ) return true;

    // If the blocktime is > the time the enforcement has become enabled + a delay to give time for the spork sharing.
    if( block.GetBlockTime() < sporkManager.GetSporkValue(SPORK_16_MINING_SIGNATURE_ENFORCEMENT)){
        return true;
    }

    // If we are syncing dont check the signatures of blocks nMiningSignaturePastTimeCutoff seconds in the past.
    if( GetAdjustedTime() > block.GetBlockTime() + nMiningSignaturePastTimeCutoff ){
        return true;
    }

    // If we dont have at least 2 outputs in the coinbase we very likely won't have a signature.
    if( !block.vtx.size() || block.vtx[0].vout.size() < 2 ){
        return false;
    }

    // Second output of the coinbase needs to be the signature.
    const CScript &sigScript = block.vtx[0].vout[1].scriptPubKey;

    // Check if it is an OP_RETURN and if the startvalue is OP_DATA_MINING_FLAG
    if( sigScript.size() == nMiningSignatureScriptLength &&
        sigScript[0] == OP_RETURN && sigScript[1] == OP_DATA_MINING_FLAG ){

        auto *pKeyMap = &mapMiningKeysMainnet;

        if( TestNet() ) pKeyMap = &mapMiningKeysTestnet;

        int64_t nEnabledKeys = sporkManager.GetSporkValue(SPORK_17_MINING_SIGNATURE_PUBKEYS_ENABLED);
        uint8_t nKeyIdx = sigScript[2];

        if( nEnabledKeys & (1 << nKeyIdx) && nKeyIdx <= pKeyMap->size() - 1 ){

            CSmartAddress signAddress = pKeyMap->at(nKeyIdx);
            CKeyID keyId;

            std::vector<unsigned char> vchSig(sigScript.begin() + 3, sigScript.end() );

            if( signAddress.IsValid() && signAddress.GetKeyID(keyId) ){

                CDataStream ss(SER_GETHASH, 0);
                ss << strMessageMagic;
                ss << std::to_string(pindex->nHeight);

                CPubKey pubkey;
                if (!pubkey.RecoverCompact(Hash(ss.begin(), ss.end()), vchSig)){

                    LogPrintf("SmartMining::CheckSignature -- The signature did not match the message digest.");
                    return false;
                }

                if (!(CSmartAddress(pubkey.GetID()) == signAddress)){

                    LogPrintf("SmartMining::CheckSignature -- VerifyMessage() failed\n");
                    return false;
                }

                LogPrintf("SmartMining::CheckSignature -- Valid at block %d!\n", pindex->nHeight);

                return true;

            }else{
                LogPrintf("SmartMining::CheckSignature -- Invalid address found: %s\n", signAddress.ToString());
            }

        }else{
            LogPrintf("SmartMining::CheckSignature -- Key disabled or out of range: %d\n", nKeyIdx);
        }

    }else{
        LogPrintf("SmartMining::CheckSignature -- Signing output missing. %s\n", block.vtx[0].ToString());
    }

    return false;
}

CAmount GetMiningReward(CBlockIndex * pindex, CAmount blockReward)
{
    return blockReward / 20; // 5%
}

void SmartMining::FillPayment(CMutableTransaction& coinbaseTx, int nHeight, CBlockIndex * pindexPrev, CAmount blockReward)
{
    coinbaseTx.vout[0].nValue = GetMiningReward(pindexPrev, blockReward);
}

bool SmartMining::Validate(const CBlock &block, CBlockIndex *pindex, CValidationState& state, CAmount nFees, bool fJustCheck)
{
    const CChainParams& chainparams = Params();
    CAmount coinbase = block.vtx[0].GetValueOut();
    CAmount blockReward = GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
    CAmount miningReward = GetMiningReward(pindex, blockReward);
    CAmount hiveReward = 0, nodeReward = 0, smartReward = 0;

    if( !fJustCheck && !CheckSignature(block, pindex) ){
        return state.DoS(0, error("SmartMining::Validate - signature enforcement enabled and no valid signature found."),
                                REJECT_INVALID, "invalid-mining-signature");
    }

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

    CAmount expectedCoinbase = nFees + nodeReward + hiveReward + smartReward + miningReward;

    if( pindex->nHeight > 1 && coinbase > expectedCoinbase ){
         LogPrintf("SmartMining::Validate - Coinbase too high Expected: %d.%08d! %s\n", expectedCoinbase / COIN, expectedCoinbase % COIN, block.vtx[0].ToString());
        return state.DoS(100, false, REJECT_INVALID,
                     "CTransaction::CheckTransaction() : Coinbase value too high");
    }

    return true;
}
