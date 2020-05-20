// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"
#include "consensus/consensus.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include "libzerocoin/bitcoin_bignum/bignum.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"
#include "arith_uint256.h"

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward, std::vector<unsigned char> extraNonce)
{
    static CBigNum bnProofOfWorkLimit(~arith_uint256(0) >> 20);

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << bnProofOfWorkLimit.GetCompact() << CBigNum(4).getvch() << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp)) << extraNonce;
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;

    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward, std::vector<unsigned char> extraNonce)
{
    const char* pszTimestamp = "SmartCash, Communinty Driven Cash";
    const CScript genesisOutputScript = CScript();
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward, extraNonce);
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nInstantSendKeepLock = 24;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = 227931;
        consensus.BIP34Hash = uint256S("0x000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8");
        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 10 * 55;
        consensus.nPowTargetSpacing = 55;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1462060800; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1479168000; // November 15th, 2016.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1510704000; // November 15th, 2017.

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000020d8ea371e16d853f4");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000000000000001c172f518793c3b9e83f202284615592f87fe3506ce964dcd4"); // 782700

        // smartnode params
        consensus.nSmartnodePaymentsStartBlock = HF_V1_1_SMARTNODE_HEIGHT; // not true, but it's ok as long as it's less then nSmartnodePaymentsIncreaseBlock
        consensus.nSmartnodeMinimumConfirmations = 15;

        // Smartvoting params

        consensus.nProposalValidityVoteBlocks = 4712; // ~3days
        consensus.nProposalFundingVoteBlocks = 21993; // ~2weeks
        consensus.nVotingMinYesPercent = 50;
        consensus.nVotingFilterElements = 200000;

        nMaxTipAge = 3 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nDelayGetHeadersTime = 24 * 60 * 60;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 60*30; // fulfilled requests expire in half hour

        strSporkAddress = "ShF3FXyj2BR8tXFXMxC33gjgJ9aaD2FiAv";

        /* SmartReward params */

        consensus.nRewardsConfirmationsRequired = 1;
        consensus.nRewardsPayoutStartDelay = 200;

        //! 1.2 Parameter
        consensus.nRewardsBlocksPerRound_1_2 = 47500;
        consensus.nRewardsPayouts_1_2_BlockInterval = 2;
        consensus.nRewardsPayouts_1_2_BlockPayees = 1000;

        //! 1.3 Parameter
        consensus.nRewardsBlocksPerRound_1_3 = 11000;  // 1 week
        consensus.nRewardsFirst_1_3_Round = 36; // Round 36 on 6/25 starts on block 1666600
        consensus.nRewardsPayouts_1_3_BlockStretch = 10000;
        consensus.nRewardsPayouts_1_3_BlockPayees = 100;

        consensus.strRewardsGlobalVoteProofAddress = "TBD";

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x5c;
        pchMessageStart[1] = 0xa1;
        pchMessageStart[2] = 0xab;
        pchMessageStart[3] = 0x1e;
        nDefaultPort = 9678;
        nPruneAfterHeight = 100000;

        std::vector<unsigned char> extraNonce(4);
        extraNonce[0] = 0x83;
        extraNonce[1] = 0x3e;
        extraNonce[2] = 0x00;
        extraNonce[3] = 0x00;

        genesis = CreateGenesisBlock(1496467978, 245887, 0x1e0ffff0, 2, 0 * COIN, extraNonce);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000007acc6970b812948d14ea5a0a13db0fdd07d5047c7e69101fa8b361e05a4"));
        assert(genesis.hashMerkleRoot == uint256S("0xb79187d8ce4d5ec398730dd34276248f1e7b09d98ca29b829e5e5e67ff21d462"));

        // Note that of those with the service bits flag, most only support a subset of possible options
        vSeeds.push_back(CDNSSeedData("seed.smrt.cash", "seed.smrt.cash", false));
        vSeeds.push_back(CDNSSeedData("seed.smrt.run", "seed.smrt.run", false));
        vSeeds.push_back(CDNSSeedData("seed.smrt.best", "seed.smrt.best", false));
        vSeeds.push_back(CDNSSeedData("seed.smarts.cash", "seed.smarts.cash", false));
        vSeeds.push_back(CDNSSeedData("seed1.smartcash.org", "seed1.smartcash.org", false));
        vSeeds.push_back(CDNSSeedData("seed2.smartcash.org", "seed2.smartcash.org", false));


        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,63); //S
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,18);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,191);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();

        base58Prefixes[PUBKEY_ADDRESS_V2] = std::vector<unsigned char>(1,125); //s
        base58Prefixes[SCRIPT_ADDRESS_V2] = std::vector<unsigned char>(1,110);
        base58Prefixes[SECRET_KEY_V2] =     std::vector<unsigned char>(1,237);
        base58Prefixes[EXT_PUBLIC_KEY_V2] = boost::assign::list_of(0x04)(0x20)(0xBD)(0x3F).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY_V2] = boost::assign::list_of(0x04)(0x20)(0xB9)(0x03).convert_to_container<std::vector<unsigned char> >();

        base58Prefixes[VOTE_KEY_PUBLIC] = std::vector<unsigned char>(1,125);
        base58Prefixes[VOTE_KEY_SECRET] = std::vector<unsigned char>(3,82);

        // SmartCash BIP44 coin type is '224'
        nExtCoinType = 224;

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fAllowMultipleAddressesFromGroup = false;
        fAllowMultiplePorts = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            ( 0, uint256S("0x000007acc6970b812948d14ea5a0a13db0fdd07d5047c7e69101fa8b361e05a4"))
            ( 75000,  uint256S("0x000000000002ee203026137ebc460e1886e09b9fdb0e83697e5a74976088e75c"))
            ( 170000, uint256S("0x000000000000670ff41fbb4ad819b48bfe1c35623f13297d3fbf9bf02abcd87c"))
            ( 500000, uint256S("0x00000000000016a1fa8e650e5a82babefeb9225ffe78614bc4b23cf160d16eeb"))
            ( 750000, uint256S("0x000000000000456bd57843a6650155f9c09b42c47e5a8d24418881a88ce8aa2e"))
            ( 1000000, uint256S("0x00000000000008e14776878dba228ac957a97205df4716ce1913ae4339e7aeb9"))
            ( 1030000, uint256S("0x00000000000000d7e76cc6c30a2bece10f552123ad3c9a63beceb0d553a46f04"))
            ( 1250000, uint256S("0x00000000000036b03ca216e92c83c9d0d152c1fdfac74c1bfc0cfc1cfa00f451"))
            ( 1500000, uint256S("0x0000000000001e396ce1ea9dfde2956fef0f606a5d6cbbcb1a5ba6e1081eadf5"))
            ( 1599000, uint256S("0x00000000000024edb61519ed6ebdf085f5dd25a0963103dc108b68e5f88604f3")),
            1589123846, // * UNIX timestamp of last checkpoint block
            11577739,  // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            33000.0     // * estimated number of transactions per day after checkpoint
        };
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nInstantSendKeepLock = 6;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 100;
        consensus.BIP34Height = 21111;
        consensus.BIP34Hash = uint256S("0x0000000023b3a96d3484e5abb3755c413e7d41500f8e2a5c3f0dd01299cd8ef8");
        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 10 * 55;
        consensus.nPowTargetSpacing = 55;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1462060800; // May 1st 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1493596800; // May 1st 2017

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000111");

        // smartnode params
        consensus.nSmartnodePaymentsStartBlock = HF_V1_1_SMARTNODE_HEIGHT + 1000;

        // Smartvoting params

        consensus.nProposalValidityVoteBlocks = 130; // ~2hour
        consensus.nProposalFundingVoteBlocks = 390; // ~6hours
        consensus.nVotingMinYesPercent = 50;
        consensus.nVotingFilterElements = 200000;

        nMaxTipAge = 3 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nDelayGetHeadersTime = 24 * 60 * 60;

        nFulfilledRequestExpireTime = 60*30; // fulfilled requests expire in half hour

        strSporkAddress = "TTUR2YweEsouT7nnqLGn3LgoykhPnFQkSY";

        /* SmartReward params */

        consensus.nRewardsConfirmationsRequired = 1;
        consensus.nRewardsPayoutStartDelay = 10;

        //! 1.2 Parameter
        consensus.nRewardsBlocksPerRound_1_2 = 100;
        consensus.nRewardsPayouts_1_2_BlockInterval = 2;
        consensus.nRewardsPayouts_1_2_BlockPayees = 1000;

        //! 1.3 Parameter
        consensus.nRewardsBlocksPerRound_1_3 = 100;
        consensus.nRewardsFirst_1_3_Round = 10; // block 201 start 1_2_8 401 start 1_3
        consensus.nRewardsPayouts_1_3_BlockStretch = 80;
        consensus.nRewardsPayouts_1_3_BlockPayees = 10;

        consensus.strRewardsGlobalVoteProofAddress = "TTUR2YweEsouT7nnqLGn3LgoykhPnFQkSY";

        pchMessageStart[0] = 0xcf;
        pchMessageStart[1] = 0xfc;
        pchMessageStart[2] = 0xbe;
        pchMessageStart[3] = 0xea;
        vAlertPubKey = ParseHex("048240a8748a80a286b270ba126705ced4f2ce5a7847b3610ea3c06513150dade2a8512ed5ea86320824683fc0818f0ac019214973e677acd1244f6d0571fc5103");
        nDefaultPort = 19678;
        nPruneAfterHeight = 1000;

        std::vector<unsigned char> extraNonce(4);
        extraNonce[0] = 0x09;
        extraNonce[1] = 0x00;
        extraNonce[2] = 0x00;
        extraNonce[3] = 0x00;

        genesis = CreateGenesisBlock(1496467978, 420977, 0x1e0ffff0, 2, 0 * COIN,extraNonce);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x0000027235b5679bcd28c90d03d4bf1a9ba4c07c4efcc1c87d6c68cce25e6e5d"));
        assert(genesis.hashMerkleRoot == uint256S("0xb344094bc70d6a82c2c33f6d21005b78d83524b4f976b8eacf0e71ccc6488aee"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.push_back(CDNSSeedData("testnet.smrt.run", "testnet.smrt.run", true));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,65); //T
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,21);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,193);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        base58Prefixes[PUBKEY_ADDRESS_V2] = std::vector<unsigned char>(1,127); //t
        base58Prefixes[SCRIPT_ADDRESS_V2] = std::vector<unsigned char>(1,13); //6
        base58Prefixes[SECRET_KEY_V2] = std::vector<unsigned char>(1,130); //u
        base58Prefixes[EXT_PUBLIC_KEY_V2] = boost::assign::list_of(0x7F)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY_V2] = boost::assign::list_of(0x7F)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();

        base58Prefixes[VOTE_KEY_PUBLIC] = std::vector<unsigned char>(1,112);
        base58Prefixes[VOTE_KEY_SECRET] = std::vector<unsigned char>(3,160);

        // SmartCash BIP44 coin type is '224'
        nExtCoinType = 224;

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;


        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            ( 0, uint256S("0x0000027235b5679bcd28c90d03d4bf1a9ba4c07c4efcc1c87d6c68cce25e6e5d")),
            1337966069,
            1488,
            300
        };

    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = -1; // BIP34 has not necessarily activated on regtest
        consensus.BIP34Hash = uint256();
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 10 * 55; // two weeks
        consensus.nPowTargetSpacing = 55;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 999999999999ULL;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        vAlertPubKey = ParseHex("04517d8a699cb43d3938d7b24faaff7cda448ca4ea267723ba614784de661949bf632d6304316b244646dea079735b9a6fc4af804efb4752075b9fe2245e14e412");
        nDefaultPort = 18444;
        nPruneAfterHeight = 1000;

        std::vector<unsigned char> extraNonce(4);
        extraNonce[0] = 0x09;
        extraNonce[1] = 0x00;
        extraNonce[2] = 0x00;
        extraNonce[3] = 0x00;

        genesis = CreateGenesisBlock(1296688602, 2, 0x207fffff, 1, 50 * COIN,extraNonce);
        consensus.hashGenesisBlock = genesis.GetHash();
        //assert(consensus.hashGenesisBlock == uint256S("0x0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"));
        //assert(genesis.hashMerkleRoot == uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of
            ( 0, uint256S("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")),
            0,
            0,
            0
        };
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        // SmartCash BIP44 coin type is '224'
        nExtCoinType = 224;
    }

    void UpdateBIP9Parameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
    {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
    }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
            return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
            return testNetParams;
    else if (chain == CBaseChainParams::REGTEST)
            return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

void UpdateRegtestBIP9Parameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    regTestParams.UpdateBIP9Parameters(d, nStartTime, nTimeout);
}
