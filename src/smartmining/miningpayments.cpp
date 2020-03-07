// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartmining/miningpayments.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "messagesigner.h"
#include "wallet/wallet.h"

#include "smartnode/smartnodeman.h"
#include "smartnode/smartnodepayments.h"
#include "smartrewards/rewardspayments.h"
#include "smarthive/hive.h"
#include "smarthive/hivepayments.h"

// Here is the place to later research with the possible mining adjustments.

CCriticalSection cs_miningkeys;

static CSmartAddress globalSigningAddress;

std::map<uint8_t, CSmartAddress> mapMiningKeysMainnet = {
    {0, CSmartAddress("SZFQHEYJ6tVZqW8QcV3GPJEaREfrkJbTYi")},
    {1, CSmartAddress("SeuLj3mmoSibWVqiNVt9UxUCPrUZKQe7G5")},
    {2, CSmartAddress("SPwumTCoMsS3HLED2b4vvZGdpHpN246QvV")},
    {3, CSmartAddress("SXz43RBpTW1vJh86eVtdy7N162fZcia6uP")},
    {4, CSmartAddress("SViHBzAvF9s2fH4AGLoeorJJVATAFqsaM7")},
    {5, CSmartAddress("STHKPHPN7ZaL86WCG9oqbhZ5SAb6wEZwue")},
    {6, CSmartAddress("SU1taLwj5okRuPn57k7kkbLo1KLUPJtyU4")},
    {7, CSmartAddress("SfrpuFW3h2TmV9u1TnUBWyXJZcPwFeQq4k")},
    {8, CSmartAddress("SXkiaN9LDm69jTtBgGKCaFrhMgAZ3bB9Jt")},
    {9, CSmartAddress("SZCgeC53c9UZcrRj5CcMueBfMcU59L2aBG")},
    {10, CSmartAddress("Sa6bviP4Lo2brtWNoN2uSc9tT1k9Yub5mz")},
    {11, CSmartAddress("ShKfaqNBKSe1xjKM1CbofLof8UDzGfhjxg")},
    {12, CSmartAddress("SZgdrHJ4mbnDZ1TJUiWFewvLLEQ6SxCTSd")},
    {13, CSmartAddress("SaTBqagBcB19kUonXfeABRxpffafnT93do")},
    {14, CSmartAddress("SdCDMJgkXCBVQQkgTXQpzZgknP8ep4ueFt")},
    {15, CSmartAddress("SUSqKDC6MabJcM7wtTxtCFsmRtj3QkRjeX")},
    {16, CSmartAddress("SgACAmcq86narubYiewzU4eDi7HjEtuoxB")},
    {17, CSmartAddress("SYsS4txdYXGwHgupBVmJSsNXGvTkZdwxUB")},
    {18, CSmartAddress("SN2XZKm2hqFZ3rp5GrT1Hupvni8GgdUiPo")},
    {19, CSmartAddress("SSxjY3DHCuFyFPrt7VtaNF7r6oaEZRcihX")},
    {20, CSmartAddress("ScUYQw4Y4JgZoWHuioNspnjDp26F5ffeJv")},
    {21, CSmartAddress("SRfUNTSb3FQqksDZxdRzmyZWFPkaPduMEJ")},
    {22, CSmartAddress("Sbo871UwZPwAq1m2EvK5um5rM7UetpvDHa")},
    {23, CSmartAddress("ScvQr8V3LiqXt9nSXfSzBugVK3857vXE5Y")},
    {24, CSmartAddress("SgTQUNnRWqNzbgRBMiFPq2nCCHAo7b8YLc")},
    {25, CSmartAddress("SPWadqBzsjFmbHe4FxEjBDkUhn2pQFGg4M")},
    {26, CSmartAddress("SNBp1dgX9wN4PjTYfUdt3Vx5gnLnbXPKji")},
    {27, CSmartAddress("SQMLRfW63zJ6HTQG97HvgZRQiQKT9dy6Xf")},
    {28, CSmartAddress("SikHT4yvfVEWk1FLVGPyTqjcxRqEF1zv7c")},
    {29, CSmartAddress("SXtuDtsjCjDqMBaiQj5x5qJdj2ExP6kV1f")},
    {30, CSmartAddress("SRXEMkMDakiiwWDU33mh6J1FGHTwLgTCRK")},
    {31, CSmartAddress("SNJ1wWJDGdczvw8FyK4YAQkRH3PNQfpREN")},
    {32, CSmartAddress("SNtZVgzEC3GxWnn2uaBJ36eQHkjt9bkFwC")},
    {33, CSmartAddress("SPQkaQ1pSMm2MHdujPJiqgUQoWuaUC8gFn")},
    {34, CSmartAddress("SPFkVdD8Ts5BNU6bvcMdwp1H1hUacEiav4")},
    {35, CSmartAddress("SewpPWuu3Ef4jaTs7v2tsbmC3odpSVYpkn")},
    {36, CSmartAddress("ScHxXfMNukHhKQW2GtrBBZkoNCjKQ3Y98K")},
    {37, CSmartAddress("SkBzN6rauivLu5mu4Hmg76QvhZuiNNEd5M")},
    {38, CSmartAddress("SU26GX71Suu742zHiyuBmxvMt9ajL7irw4")},
    {39, CSmartAddress("SYbLxc3QFkR3yBQ2P1KCZHiuo7HsGBRRt8")},
    {40, CSmartAddress("SYo5ptb51mCbvEtHUfpzgHYVjAHqPHTvgd")},
    {41, CSmartAddress("SSdNcKpc8g5cLUY4mb99ZZiEeohRckwCph")},
    {42, CSmartAddress("SRRNUBZHY1t52c9yoY2S3pYapPCiqVahr3")},
    {43, CSmartAddress("SZUCYnjcWLFwAd4i5bDjkSVtj6vsvWWStx")},
    {44, CSmartAddress("ShQh5raLRHiTVSud45tagoQJYMahMBUMT5")},
    {45, CSmartAddress("Scam1ymgercP38nKa7fK6ijAoJ2TBV4Why")},
    {46, CSmartAddress("SQprnEjquarnA3ZaMFMtzPNSmx7o7pvFqD")},
    {47, CSmartAddress("ShQuwVyMjCWXiZJxs69T1efhYoiqFwjpj6")},
    {48, CSmartAddress("SkdMXLZQo3v8NCgyq51rVjntA6R5nRtkJg")},
    {49, CSmartAddress("SfgXycGYCa1rrTSHaupcqMgBtHwrPhKx8N")},
    {50, CSmartAddress("STSUHn6tXGpoxAkNiiBCDQESoTVMN1KBPo")},
    {51, CSmartAddress("SdmauPTQ1VxDXCzMiXbC83nnPA5LF75zcS")},
    {52, CSmartAddress("SgGr24zunCX2Hkh9UXsTuw8Tc16vtpe1ZA")},
    {53, CSmartAddress("SRYMVBxFBKTmV5k2CXxdFXg2d2sEsfzwZ4")},
    {54, CSmartAddress("SMvFccb3YzGwWt9C67bSD2qvxDCJKJq1nk")},
    {55, CSmartAddress("SRe8VuyubfQ3xY3msUEJWyAcyJH5vdiMrz")},
    {56, CSmartAddress("Sb8TNjBMBn8hEA3wmkRS25JqdfNeu1ahHq")},
    {57, CSmartAddress("Sjhj7YYTuT1RFGN28vvyQv7zjrTNUid1vQ")},
    {58, CSmartAddress("SRLCZGqCybmbQrKKbeMV4fKbnA6dJTvor3")},
    {59, CSmartAddress("SRNeZraHjj5VNWC2k6Z3tsCw9ncV9QViJv")},
    {60, CSmartAddress("SimxfrTk5V8F8UT9eDJxfV8yt1mrXwnx1X")},
    {61, CSmartAddress("Sch2EY1y3ozu2WgFUtxBojRmKUwWgxyUSx")},
    {62, CSmartAddress("SZqwUufMavPQRNtft9LbStqnQxPT2sgxn3")},
    {63, CSmartAddress("SicJ4xb7gguvFRUBraAezDtjoHsUQ3qymZ")}
};

std::map<uint8_t, CSmartAddress> mapMiningKeysTestnet = {
    {0,  CSmartAddress("TUcdknEDtqM5cRf6YFM5xRNzcAbQuNpJoA")},
    {1,  CSmartAddress("TGwRnVCEBouA75mkfgwZ5XGH66sjXJj2iq")},
    {2,  CSmartAddress("TYkeHMSS3VBfnH8i9yRqxnR3uxjavrSpoQ")},
    {63, CSmartAddress("TFDgrpTFGUL9NZgEjTMxuF5v6pw2tKSuRu")}
};

/**
  This is a workaroud to avoid hashrate attacks from bad pools until we have a proper solution.
  It allows us to force pools to sign the blocks with a private key which a pool can receive from us.
*/

bool SmartMining::SetMiningKey(string &address)
{
    LOCK(cs_miningkeys);

    auto *pKeyMap = &mapMiningKeysMainnet;

    if( TestNet() ) pKeyMap = &mapMiningKeysTestnet;

    auto it = pKeyMap->begin();

    while (it != pKeyMap->end()){
        if( address == it->second.ToString()){
            globalSigningAddress = it->second;
            return true;
        }
        it++;
    }

    return false;
}

bool SmartMining::IsSignatureRequired(const CBlockIndex *pindex){

    // If the blockheight is less than the height the enforcement has been set to.
    if( pindex->nHeight < sporkManager.GetSporkValue(SPORK_16_MINING_SIGNATURE_ENFORCEMENT)){
        return false;
    }

    // If we are syncing dont check the signatures of blocks nMiningSignaturePastTimeCutoff seconds in the past.
    if( GetAdjustedTime() > pindex->GetBlockTime() + nMiningSignaturePastTimeCutoff ){
        return false;
    }

    return true;
}

bool SmartMining::IsSignatureRequired(const int nHeight){

    // If the blockheight is less than the height the enforcement has been set to.
    if( nHeight < sporkManager.GetSporkValue(SPORK_16_MINING_SIGNATURE_ENFORCEMENT)){
        return false;
    }

    return true;
}

static bool CheckSignature(const CBlock &block, const CBlockIndex *pindex)
{

    if( !SmartMining::IsSignatureRequired(pindex) ){
        return true;
    }

    // If we dont have at least 2 outputs in the coinbase we very likely won't have a signature.
    if( !block.vtx.size() || block.vtx[0].vout.size() < 2 ){
        return false;
    }

    // Second output of the coinbase needs to be the signature.
    const CScript &sigScript = block.vtx[0].vout[1].scriptPubKey;

    // Check if it is an OP_RETURN and if the startvalue is OP_DATA_MINING_FLAG
    if( sigScript.size() > nMiningSignatureMinScriptLength &&
        sigScript[0] == OP_RETURN && sigScript[2] == OP_RETURN_MINING_FLAG ){

        LOCK(cs_miningkeys);

        auto *pKeyMap = &mapMiningKeysMainnet;

        if( TestNet() ) pKeyMap = &mapMiningKeysTestnet;

        int64_t nEnabledKeys = sporkManager.GetSporkValue(SPORK_17_MINING_SIGNATURE_PUBKEYS_ENABLED);
        uint8_t nKeyIdx = sigScript[3];

        if( nEnabledKeys & (1 << nKeyIdx) && nKeyIdx <= pKeyMap->size() - 1 ){

            CSmartAddress signAddress = pKeyMap->at(nKeyIdx);
            CKeyID keyId;

            std::vector<unsigned char> vchSig(sigScript.begin() + 4, sigScript.end() );

            if( signAddress.IsValid() && signAddress.GetKeyID(keyId) ){

                CDataStream ss(SER_GETHASH, 0);
                ss << strMessageMagic;
                ss << pindex->nHeight;

                CPubKey pubkey;
                if (!pubkey.RecoverCompact(Hash(ss.begin(), ss.end()), vchSig)){

                    LogPrintf("SmartMining::CheckSignature -- The signature did not match the message digest.\n");
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
    if( pindex->nHeight < HF_V1_3_HEIGHT ){ 
    return blockReward / 20; // 5%
    }
    else {
    return blockReward / 100; // 1%
    }
}

void SmartMining::FillPayment(CMutableTransaction& coinbaseTx, int nHeight, CBlockIndex * pindexPrev, CAmount blockReward, CTxOut &outSignature, const CSmartAddress &signingAddress)
{
    coinbaseTx.vout[0].nValue = GetMiningReward(pindexPrev, blockReward);

    if( pwalletMain ){

        CSmartAddress validAddress;

        if( globalSigningAddress.IsValid() ){
            validAddress = globalSigningAddress;
        }else if( signingAddress.IsValid() ){
            validAddress = signingAddress;
        }

        if (!validAddress.IsValid())
        {
            LogPrintf("SmartMining::FillPayment -- The given signingAddress is invalid.\n");
            return;
        }

        LOCK(cs_miningkeys);

        auto *pKeyMap = &mapMiningKeysMainnet;

        if( TestNet() ) pKeyMap = &mapMiningKeysTestnet;

        auto searchAddress = std::find_if(pKeyMap->begin(), pKeyMap->end(), [validAddress](const std::pair<int, CSmartAddress> &pair)
        {
            return pair.second == validAddress;
        });

        if( searchAddress == pKeyMap->end() ){
            LogPrintf("SmartMining::FillPayment -- The given signingAddress is no official one.\n");
            return;
        }

        CKeyID keyID;
        if (!validAddress.GetKeyID(keyID))
        {
            LogPrintf("SmartMining::FillPayment -- The given signingAddress does not refer to a key.\n");
            return;
        }

        CKey key;
        if (!pwalletMain->GetKey(keyID, key))
        {
            LogPrintf("SmartMining::FillPayment -- Private key for the given signingAddresss is not available.\n");
            return;
        }

        CDataStream ss(SER_GETHASH, 0);
        ss << strMessageMagic;
        ss << nHeight;

        std::vector<unsigned char> vchSig;
        if (!key.SignCompact(Hash(ss.begin(), ss.end()), vchSig))
        {
            LogPrintf("SmartMining::FillPayment -- Message signing failed.\n");
            return;
        }

        std::vector<unsigned char> vSigData = {
            OP_RETURN_MINING_FLAG,
            searchAddress->first
        };

        vSigData.insert(vSigData.end(), vchSig.begin(), vchSig.end());

        CScript signingScript = CScript() << OP_RETURN << vSigData;

        outSignature = CTxOut(0, signingScript);
        coinbaseTx.vout.push_back(outSignature);
    }

}

bool SmartMining::Validate(const CBlock &block, CBlockIndex *pindex, CValidationState& state, CAmount nFees)
{
    const CChainParams& chainparams = Params();
    CAmount coinbase = block.vtx[0].GetValueOut();
    CAmount blockReward = GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
    CAmount miningReward = GetMiningReward(pindex, blockReward);
    CAmount hiveReward = 0, nodeReward = 0, smartReward = 0;

    if( !CheckSignature(block, pindex) ){
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
         LogPrintf("SmartMining::Validate - Coinbase %d.%08d!\n", coinbase / COIN, coinbase % COIN);
         LogPrintf("SmartMining::Validate - Expected Coinbase %d.%08d!\n", expectedCoinbase / COIN, expectedCoinbase % COIN);
         LogPrintf("SmartMining::Validate - Coinbase higher than Expected: %d.%08d! %s\n", expectedCoinbase / COIN, expectedCoinbase % COIN, block.vtx[0].ToString());
        return state.DoS(100, false, REJECT_INVALID,
                     "CTransaction::CheckTransaction() : Coinbase value too high");
    }

    return true;
}

