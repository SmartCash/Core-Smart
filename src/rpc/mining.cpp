// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "validation.h"
#include "miner.h"
#include "net.h"
#include "pow.h"
#include "rpc/server.h"
#include "smartnode/spork.h"
#include "txmempool.h"
#include "util.h"
#ifdef ENABLE_WALLET
#endif
#include "utilstrencodings.h"
#include "validationinterface.h"
#include "warnings.h"
#include "smartmining/miningpayments.h"
#include "smartnode/smartnodepayments.h"
#include "smartnode/smartnodesync.h"
#include "smarthive/hive.h"

#include <stdint.h>

#include <boost/assign/list_of.hpp>
#include <boost/shared_ptr.hpp>

#include <univalue.h>

using namespace std;

/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or from the last difficulty change if 'lookup' is nonpositive.
 * If 'height' is nonnegative, compute the estimate at the time when a given block was found.
 */
UniValue GetNetworkHashPS(int lookup, int height) {
    CBlockIndex *pb = chainActive.Tip();

    if (height >= 0 && height < chainActive.Height())
        pb = chainActive[height];

    if (pb == NULL || !pb->nHeight)
        return 0;

    // If lookup is -1, then use blocks since last difficulty change.
    if (lookup <= 0)
        lookup = pb->nHeight % Params().GetConsensus().DifficultyAdjustmentInterval() + 1;

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex *pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++) {
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    // In case there's a situation where minTime == maxTime, we don't want a divide by zero exception.
    if (minTime == maxTime)
        return 0;

    arith_uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    int64_t timeDiff = maxTime - minTime;

    return workDiff.getdouble() / timeDiff;
}

UniValue getnetworkhashps(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getnetworkhashps ( blocks height )\n"
            "\nReturns the estimated network hashes per second based on the last n blocks.\n"
            "Pass in [blocks] to override # of blocks, -1 specifies since last difficulty change.\n"
            "Pass in [height] to estimate the network speed at the time when a certain block was found.\n"
            "\nArguments:\n"
            "1. blocks     (numeric, optional, default=120) The number of blocks, or -1 for blocks since last difficulty change.\n"
            "2. height     (numeric, optional, default=-1) To estimate at the time of the given height.\n"
            "\nResult:\n"
            "x             (numeric) Hashes per second estimated\n"
            "\nExamples:\n"
            + HelpExampleCli("getnetworkhashps", "")
            + HelpExampleRpc("getnetworkhashps", "")
       );

    LOCK(cs_main);
    return GetNetworkHashPS(params.size() > 0 ? params[0].get_int() : 120, params.size() > 1 ? params[1].get_int() : -1);
}

UniValue generateBlocks(boost::shared_ptr<CReserveScript> coinbaseScript, int nGenerate, uint64_t nMaxTries, bool keepScript, const CSmartAddress &signingAddress)
{
    static const int nInnerLoopCount = 0x10000;
    int nHeightStart = 0;
    int nHeightEnd = 0;
    int nHeight = 0;

    {   // Don't keep cs_main locked
        LOCK(cs_main);
        nHeightStart = chainActive.Height();
        nHeight = nHeightStart;
        nHeightEnd = nHeightStart+nGenerate;
    }
    unsigned int nExtraNonce = 0;
    UniValue blockHashes(UniValue::VARR);
    while (nHeight < nHeightEnd)
    {
        std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(coinbaseScript->reserveScript, signingAddress));
        if (!pblocktemplate.get())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't create new block");
        CBlock *pblock = &pblocktemplate->block;
        {
            LOCK(cs_main);
            IncrementExtraNonce(pblock, chainActive.Tip(), nExtraNonce);
        }
        while (nMaxTries > 0 && pblock->nNonce < nInnerLoopCount && !CheckProofOfWork(nHeight, pblock->GetHash(), pblock->nBits, Params().GetConsensus())) {
            ++pblock->nNonce;
            --nMaxTries;
        }
        if (nMaxTries == 0) {
            break;
        }
        if (pblock->nNonce == nInnerLoopCount) {
            continue;
        }
        CValidationState state;
        if (!ProcessNewBlock(Params(), pblock, true, NULL, NULL))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted");
        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());

        //mark script as important because it was used at least for one coinbase output if the script came from the wallet
        if (keepScript)
        {
            coinbaseScript->KeepScript();
        }
    }
    return blockHashes;
}

UniValue getgenerate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getgenerate\n"
            "\nReturn if the server is set to generate coins or not. The default is false.\n"
            "It is set with the command line argument -gen (or " + std::string(BITCOIN_CONF_FILENAME) + " setting gen)\n"
            "It can also be set with the setgenerate call.\n"
            "\nResult\n"
            "true|false      (boolean) If the server is set to generate coins or not\n"
            "\nExamples:\n"
            + HelpExampleCli("getgenerate", "")
            + HelpExampleRpc("getgenerate", "")
        );

    LOCK(cs_main);
    return GetBoolArg("-gen", DEFAULT_GENERATE);
}

UniValue generate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "generate numblocks ( maxtries )\n"
            "\nMine up to numblocks blocks immediately (before the RPC call returns)\n"
            "\nArguments:\n"
            "1. numblocks       (numeric, required) How many blocks are generated immediately.\n"
            "2. maxtries        (numeric, required) How many iterations to try (default = 1000000).\n"
            "3. signingAddress  (string, optional) The address being used to sign the block.\n"
            "\nResult\n"
            "[ blockhashes ]     (array) hashes of blocks generated\n"
            "\nExamples:\n"
            "\nGenerate 11 blocks\n"
            + HelpExampleCli("generate", "11 1000 \"signingaddress\"")
        );

    int nGenerate = params[0].get_int();
    uint64_t nMaxTries = 1000000;
    if (params.size() > 1) {
        nMaxTries = params[1].get_int();
    }

    CSmartAddress signingAddress;

    if (params.size() > 2) {
        signingAddress = CSmartAddress(params[2].get_str());
    }

    boost::shared_ptr<CReserveScript> coinbaseScript;
    GetMainSignals().ScriptForMining(coinbaseScript);

    if (!smartnodeSync.IsSynced() && !fDebug)
         throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "SmartCash is syncing with network...");

    // If the keypool is exhausted, no script is returned at all.  Catch this.
    if (!coinbaseScript)
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    //throw an error if no script was provided
    if (coinbaseScript->reserveScript.empty())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No coinbase script available (mining requires a wallet)");

    return generateBlocks(coinbaseScript, nGenerate, nMaxTries, true, signingAddress);
}

UniValue generatetoaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(
            "generatetoaddress numblocks address (maxtries)\n"
            "\nMine blocks immediately to a specified address (before the RPC call returns)\n"
            "\nArguments:\n"
            "1. numblocks      (numeric, required) How many blocks are generated immediately.\n"
            "2. address        (string, required) The address to send the newly generated smartcash to.\n"
            "3. maxtries       (numeric, required) How many iterations to try (default = 1000000).\n"
            "4. signingAddress (string, optional) The address being used to sign the block.\n"
            "\nResult\n"
            "[ blockhashes ]     (array) hashes of blocks generated\n"
            "\nExamples:\n"
            "\nGenerate 11 blocks to myaddress\n"
            + HelpExampleCli("generatetoaddress", "11 \"myaddress\" 1000 \"signingaddress\"")
        );

    int nGenerate = params[0].get_int();
    uint64_t nMaxTries = 1000000;
    if (params.size() > 2) {
        nMaxTries = params[2].get_int();
    }

    CSmartAddress signingAddress;

    if (params.size() > 3) {
        signingAddress = CSmartAddress(params[3].get_str());
    }

    CBitcoinAddress address(params[1].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address");

    boost::shared_ptr<CReserveScript> coinbaseScript(new CReserveScript());
    coinbaseScript->reserveScript = GetScriptForDestination(address.Get());

    return generateBlocks(coinbaseScript, nGenerate, nMaxTries, false, signingAddress);
}

UniValue setgenerate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setgenerate generate ( genproclimit )\n"
            "\nSet 'generate' true or false to turn generation on or off.\n"
            "Generation is limited to 'genproclimit' processors, -1 is unlimited.\n"
            "See the getgenerate call for the current setting.\n"
            "\nArguments:\n"
            "1. generate         (boolean, required) Set to true to turn on generation, false to turn off.\n"
            "2. genproclimit     (numeric, optional) Set the processor limit for when generation is on. Can be -1 for unlimited.\n"
            "\nExamples:\n"
            "\nSet the generation on with a limit of one processor\n"
            + HelpExampleCli("setgenerate", "true 1") +
            "\nCheck the setting\n"
            + HelpExampleCli("getgenerate", "") +
            "\nTurn off generation\n"
            + HelpExampleCli("setgenerate", "false") +
            "\nUsing json rpc\n"
            + HelpExampleRpc("setgenerate", "true, 1")
        );

    if (Params().MineBlocksOnDemand())
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Use the generate method instead of setgenerate on this network");

    bool fGenerate = true;
    if (params.size() > 0)
        fGenerate = params[0].get_bool();

    int nGenProcLimit = GetArg("-genproclimit", DEFAULT_GENERATE_THREADS);
    if (params.size() > 1)
    {
        nGenProcLimit = params[1].get_int();
        if (nGenProcLimit == 0)
            fGenerate = false;
    }

    mapArgs["-gen"] = (fGenerate ? "1" : "0");
    mapArgs ["-genproclimit"] = itostr(nGenProcLimit);
    GenerateBitcoins(fGenerate, nGenProcLimit, Params(), *g_connman);

    return NullUniValue;
}

UniValue getmininginfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getmininginfo\n"
            "\nReturns a json object containing mining-related information."
            "\nResult:\n"
            "{\n"
            "  \"blocks\": nnn,             (numeric) The current block\n"
            "  \"currentblocksize\": nnn,   (numeric) The last block size\n"
            "  \"currentblockweight\": nnn, (numeric) The last block weight\n"
            "  \"currentblocktx\": nnn,     (numeric) The last block transaction\n"
            "  \"difficulty\": xxx.xxxxx    (numeric) The current difficulty\n"
            "  \"errors\": \"...\"            (string) Current errors\n"
            "  \"networkhashps\": nnn,      (numeric) The network hashes per second\n"
            "  \"pooledtx\": n              (numeric) The size of the mempool\n"
            "  \"testnet\": true|false      (boolean) If using testnet or not\n"
            "  \"chain\": \"xxxx\",           (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmininginfo", "")
            + HelpExampleRpc("getmininginfo", "")
        );


    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("blocks",           (int)chainActive.Height());
    obj.pushKV("currentblocksize", (uint64_t)nLastBlockSize);
    obj.pushKV("currentblockweight", (uint64_t)nLastBlockWeight);
    obj.pushKV("currentblocktx",   (uint64_t)nLastBlockTx);
    obj.pushKV("difficulty",       (double)GetDifficulty());
    obj.pushKV("networkhashps",    getnetworkhashps(params, false));
    obj.pushKV("pooledtx",         (uint64_t)mempool.size());
    obj.pushKV("chain",            Params().NetworkIDString());
    obj.pushKV("warnings",         GetWarnings("statusbar"));

    return obj;
}


// NOTE: Unlike wallet RPC (which use BTC values), mining RPCs follow GBT (BIP 22) in using satoshi amounts
UniValue prioritisetransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "prioritisetransaction <txid> <priority delta> <fee delta>\n"
            "Accepts the transaction into mined blocks at a higher (or lower) priority\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id.\n"
            "2. priority delta (numeric, required) The priority to add or subtract.\n"
            "                  The transaction selection algorithm considers the tx as it would have a higher priority.\n"
            "                  (priority of a transaction is calculated: coinage * value_in_satoshis / txsize) \n"
            "3. fee delta      (numeric, required) The fee value (in satoshis) to add (or subtract, if negative).\n"
            "                  The fee is not actually paid, only the algorithm for selecting transactions into a block\n"
            "                  considers the transaction as it would have paid a higher (or lower) fee.\n"
            "\nResult\n"
            "true              (boolean) Returns true\n"
            "\nExamples:\n"
            + HelpExampleCli("prioritisetransaction", "\"txid\" 0.0 10000")
            + HelpExampleRpc("prioritisetransaction", "\"txid\", 0.0, 10000")
        );

    LOCK(cs_main);

    uint256 hash = ParseHashStr(params[0].get_str(), "txid");
    CAmount nAmount = params[2].get_int64();

    mempool.PrioritiseTransaction(hash, params[0].get_str(), params[1].get_real(), nAmount);
    return true;
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const CValidationState& state)
{
    if (state.IsValid())
        return NullUniValue;

    std::string strRejectReason = state.GetRejectReason();
    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, strRejectReason);
    if (state.IsInvalid())
    {
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

std::string gbt_vb_name(const Consensus::DeploymentPos pos) {
    const struct BIP9DeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
    std::string s = vbinfo.name;
    if (!vbinfo.gbt_force) {
        s.insert(s.begin(), '!');
    }
    return s;
}

UniValue getblocktemplate(const UniValue& params, bool fHelp)
{

    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "getblocktemplate ( SigningAddress )\n"
            "\nReturns a block template miners must comply with to create blocks that will become accepted by"
            " the SmartCash network.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\" : n,                     (numeric) The preferred block version\n"
            "  \"previousblockhash\" : \"xxxx\",    (string) The hash of current highest block\n"
            "  \"signing_required\" : \"xxxx\",     (bool) If this is true block signing is currently enforced.\n"
            "  \"coinbase\" : {                     (object) contents of the coinbase transaction that should be included in the next block\n"
            "       \"mining\": n,                  (numeric) value of the SmartMining reward\n"
            "       \"signature\": \"xxxx\",        (string) signature script encoded in hexadecimal if a valid SigningAddress was provided.\n"
            "       \"smarthives\": [               (array) contains all SmartHive payouts required for the next block\n"
            "         {\n"
            "           \"payee\": \"xxx\",         (string) payee's public address as string.\n"
            "           \"script\": \"xxx\",        (string) payee's script encoded in hexadecimal (byte-for-byte)\n"
            "           \"amount\": n               (numeric) value of the SmartHive reward\n"
            "         }\n"
            "         ,...\n"
            "       ],\n"
            "       \"smartnodes\": [               (array) contains all SmartNode payouts required for the next block\n"
            "         {\n"
            "           \"payee\": \"xxx\",         (string) payee's public address as string.\n"
            "           \"script\": \"xxx\",        (string) payee's script encoded in hexadecimal (byte-for-byte)\n"
            "           \"amount\": n               (numeric) value of the SmartNode reward\n"
            "         }\n"
            "         ,...\n"
            "       ],\n"
            "       \"smartrewards\": [             (array) contains all SmartReward payouts required for the next block\n"
            "         {\n"
            "           \"payee\": \"xxx\",         (string) payee's public address as string.\n"
            "           \"script\": \"xxx\",        (string) payee's script encoded in hexadecimal (byte-for-byte)\n"
            "           \"amount\": n               (numeric) value of the SmartReward\n"
            "         }\n"
            "         ,...\n"
            "       ]\n"
            "  },\n"
            "  \"transactions\" : [                  (array) contents of non-coinbase transactions that should be included in the next block\n"
            "      {\n"
            "         \"data\" : \"xxxx\",           (string) transaction data encoded in hexadecimal (byte-for-byte)\n"
            "         \"txid\" : \"xxxx\",           (string) transaction id encoded in little-endian hexadecimal\n"
            "         \"hash\" : \"xxxx\",           (string) hash encoded in little-endian hexadecimal (including witness data)\n"
            "         \"depends\" : [                (array) array of numbers \n"
            "             n                          (numeric) transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is\n"
            "             ,...\n"
            "         ],\n"
            "         \"fee\": n,                    (numeric) difference in value between transaction inputs and outputs (in satoshis); for coinbase transactions, this is a negative Number of the total collected block fees (ie, not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT assume there isn't one\n"
            "         \"sigops\" : n,                (numeric) total SigOps cost, as counted for purposes of block limits; if key is not present, sigop cost is unknown and clients MUST NOT assume it is zero\n"
            "         \"weight\" : n,                (numeric) total transaction weight, as counted for purposes of block limits\n"
            "         \"required\" : true|false      (boolean) if provided and true, this transaction must be in the final block\n"
            "      }\n"
            "      ,...\n"
            "  ],\n"
            "  \"longpollid\" : \"xxxx\",            (string) The longpollid hash\n"
            "  \"target\" : \"xxxx\",                (string) The hash target\n"
            "  \"mintime\" : xxx,                    (numeric) The minimum timestamp appropriate for next block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mutable\" : [                       (array of string) list of ways the block template may be changed \n"
            "     \"value\"                          (string) A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'\n"
            "     ,...\n"
            "  ],\n"
            "  \"noncerange\" : \"00000000ffffffff\",  (string) A range of valid nonces\n"
            "  \"sigoplimit\" : n,                     (numeric) limit of sigops in blocks\n"
            "  \"sizelimit\" : n,                      (numeric) limit of block size\n"
            "  \"weightlimit\" : n,                    (numeric) limit of block weight\n"
            "  \"curtime\" : ttt,                      (numeric) current timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"bits\" : \"xxxxxxxx\",                (string) compressed target of next block\n"
            "  \"height\" : n                          (numeric) The height of the next block\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblocktemplate", "\"signingaddress\"")
            + HelpExampleRpc("getblocktemplate", "\"signingaddress\"")
         );

    LOCK(cs_main);

    if(!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if (g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "SmartCash Core is not connected!");

    if (IsInitialBlockDownload() && !fDebug)
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "SmartCash is downloading blocks...");

    if (!smartnodeSync.IsSynced() && !fDebug)
         throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "SmartCash is syncing with network...");

    CSmartAddress signingAddress;

    if (params.size() == 1) {
        signingAddress = CSmartAddress(params[0].get_str());
    }

    static unsigned int nTransactionsUpdatedLast;

    // Update block
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static CBlockTemplate* pblocktemplate;
    if (pindexPrev != chainActive.Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5))
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = NULL;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = chainActive.Tip();
        nStart = GetTime();

        // Create new block
        if(pblocktemplate)
        {
            delete pblocktemplate;
            pblocktemplate = NULL;
        }
        CScript scriptDummy = CScript() << OP_TRUE;
        pblocktemplate = BlockAssembler(Params()).CreateNewBlock(scriptDummy, signingAddress);
        if (!pblocktemplate) throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;

    }

    CBlock* pblock = &pblocktemplate->block; // pointer for convenience
    const Consensus::Params& consensusParams = Params().GetConsensus();

    // Update nTime
    UpdateTime(pblock, consensusParams, pindexPrev);
    pblock->nNonce = 0;

    // NOTE: If at some point we support pre-segwit miners post-segwit-activation, this needs to take segwit support into consideration
    //const bool fPreSegWit = (THRESHOLD_ACTIVE != VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_SEGWIT, versionbitscache));
    const bool fPreSegWit = false;

    UniValue transactions(UniValue::VARR);
    map<uint256, int64_t> setTxIndex;
    int i = 0;
    BOOST_FOREACH (CTransaction& tx, pblock->vtx) {
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase())
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.pushKV("data", EncodeHexTx(tx));
        entry.pushKV("txid", txHash.GetHex());
        entry.pushKV("hash", tx.GetWitnessHash().GetHex());

        UniValue deps(UniValue::VARR);
        BOOST_FOREACH (const CTxIn &in, tx.vin)
        {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.pushKV("depends", deps);

        int index_in_template = i - 1;
        entry.pushKV("fee", pblocktemplate->vTxFees[index_in_template]);
        int64_t nTxSigOps = pblocktemplate->vTxSigOpsCost[index_in_template];
        if (fPreSegWit) {
            assert(nTxSigOps % WITNESS_SCALE_FACTOR == 0);
            nTxSigOps /= WITNESS_SCALE_FACTOR;
        }
        entry.pushKV("sigops", nTxSigOps);
        entry.pushKV("weight", GetTransactionWeight(tx));

        transactions.push_back(entry);
    }

    UniValue coinbase(UniValue::VOBJ);
    coinbase.pushKV("mining", (int64_t)pblock->vtx[0].vout[0].nValue);

    std::string signature = "";

    if( !pblock->outSignature.IsNull() ){
        CScript signingScript = pblock->outSignature.scriptPubKey;
        signature = HexStr(signingScript.begin(), signingScript.end());
    }

    coinbase.pushKV("signature",signature);

    {
        UniValue smartHivePayouts(UniValue::VARR);
        if(pblock->voutSmartHives.size()) {

            BOOST_FOREACH(const CTxOut &out, pblock->voutSmartHives)
            {
                UniValue payout(UniValue::VOBJ);
                CTxDestination destination;
                ExtractDestination(out.scriptPubKey, destination);
                CSmartAddress address(destination);
                payout.pushKV("payee", address.ToString().c_str());
                payout.pushKV("script", HexStr(out.scriptPubKey.begin(), out.scriptPubKey.end()));
                payout.pushKV("amount", out.nValue);

                smartHivePayouts.push_back(payout);
            }
        }

        coinbase.pushKV("smarthives", smartHivePayouts);
    }
    {
        UniValue smartNodePayouts(UniValue::VARR);
        if(pblock->voutSmartNodes.size()) {

            BOOST_FOREACH(const CTxOut &out, pblock->voutSmartNodes)
            {
                UniValue payout(UniValue::VOBJ);
                CTxDestination destination;
                ExtractDestination(out.scriptPubKey, destination);
                CSmartAddress address(destination);
                payout.pushKV("payee", address.ToString().c_str());
                payout.pushKV("script", HexStr(out.scriptPubKey.begin(), out.scriptPubKey.end()));
                payout.pushKV("amount", out.nValue);

                smartNodePayouts.push_back(payout);
            }
        }
        coinbase.pushKV("smartnodes", smartNodePayouts);
    }
    {
        UniValue smartRewardPayouts(UniValue::VARR);
        if(pblock->voutSmartRewards.size()) {

            BOOST_FOREACH(const CTxOut &out, pblock->voutSmartRewards)
            {
                UniValue payout(UniValue::VOBJ);
                CTxDestination destination;
                ExtractDestination(out.scriptPubKey, destination);
                CSmartAddress address(destination);
                payout.pushKV("payee", address.ToString().c_str());
                payout.pushKV("script", HexStr(out.scriptPubKey.begin(), out.scriptPubKey.end()));
                payout.pushKV("amount", out.nValue);

                smartRewardPayouts.push_back(payout);
            }
        }

        coinbase.pushKV("smartrewards", smartRewardPayouts);
    }

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    UniValue aMutable(UniValue::VARR);
    aMutable.push_back("time");
    aMutable.push_back("transactions");
    aMutable.push_back("prevblock");

    UniValue result(UniValue::VOBJ);

    result.pushKV("chain", Params().NetworkIDString());
    result.pushKV("version", pblock->nVersion);
    result.pushKV("previousblockhash", pblock->hashPrevBlock.GetHex());
    result.pushKV("signing_required", SmartMining::IsSignatureRequired(pindexPrev->nHeight +1));
    result.pushKV("coinbase", coinbase);
    result.pushKV("transactions", transactions);
    result.pushKV("longpollid", chainActive.Tip()->GetBlockHash().GetHex() + i64tostr(nTransactionsUpdatedLast));
    result.pushKV("target", hashTarget.GetHex());
    result.pushKV("mintime", (int64_t)pindexPrev->GetMedianTimePast()+1);
    result.pushKV("mutable", aMutable);
    result.pushKV("noncerange", "00000000ffffffff");
    int64_t nSigOpLimit = MAX_BLOCK_SIGOPS_COST;
    if (fPreSegWit) {
        assert(nSigOpLimit % WITNESS_SCALE_FACTOR == 0);
        nSigOpLimit /= WITNESS_SCALE_FACTOR;
    }
    result.pushKV("sigoplimit", nSigOpLimit);
    result.pushKV("sizelimit", (int64_t)MAX_BLOCK_SERIALIZED_SIZE);
    result.pushKV("weightlimit", (int64_t)MAX_BLOCK_WEIGHT);
    result.pushKV("curtime", pblock->GetBlockTime());
    result.pushKV("bits", strprintf("%08x", pblock->nBits));
    result.pushKV("height", (int64_t)(pindexPrev->nHeight+1));

    return result;
}

class submitblock_StateCatcher : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    submitblock_StateCatcher(const uint256 &hashIn) : hash(hashIn), found(false), state() {}

protected:
    virtual void BlockChecked(const CBlock& block, const CValidationState& stateIn) {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    };
};

UniValue submitblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "submitblock \"hexdata\" ( \"jsonparametersobject\" )\n"
            "\nAttempts to submit new block to network.\n"
            "The 'jsonparametersobject' parameter is currently ignored.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments\n"
            "1. \"hexdata\"    (string, required) the hex-encoded block data to submit\n"
            "2. \"jsonparametersobject\"     (string, optional) object of optional parameters\n"
            "    {\n"
            "      \"workid\" : \"id\"    (string, optional) if the server provided a workid, it MUST be included with submissions\n"
            "    }\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("submitblock", "\"mydata\"")
            + HelpExampleRpc("submitblock", "\"mydata\"")
        );

    CBlock block;
    if (!DecodeHexBlk(block, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    uint256 hash = block.GetHash();
    bool fBlockPresent = false;
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end()) {
            CBlockIndex *pindex = mi->second;
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                return "duplicate";
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return "duplicate-invalid";
            // Otherwise, we might only have the header - process the block before returning
            fBlockPresent = true;
        }
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end()) {
            UpdateUncommittedBlockStructures(block, mi->second, Params().GetConsensus());
        }
    }

    CValidationState state;
    submitblock_StateCatcher sc(block.GetHash());
    RegisterValidationInterface(&sc);
    bool fAccepted = ProcessNewBlock(Params(), &block, true, NULL, NULL);
    UnregisterValidationInterface(&sc);
    if (fBlockPresent)
    {
        if (fAccepted && !sc.found)
            return "duplicate-inconclusive";
        return "duplicate";
    }
    if (fAccepted)
    {
        if (!sc.found)
            return "inconclusive";
        state = sc.state;
    }
    return BIP22ValidationResult(state);
}

UniValue estimatefee(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "estimatefee nblocks\n"
            "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
            "confirmation within nblocks blocks.\n"
            "\nArguments:\n"
            "1. nblocks     (numeric)\n"
            "\nResult:\n"
            "n              (numeric) estimated fee-per-kilobyte\n"
            "\n"
            "A negative value is returned if not enough transactions and blocks\n"
            "have been observed to make an estimate.\n"
            "-1 is always returned for nblocks == 1 as it is impossible to calculate\n"
            "a fee that is high enough to get reliably included in the next block.\n"
            "\nExample:\n"
            + HelpExampleCli("estimatefee", "6")
            );

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    CFeeRate feeRate = mempool.estimateFee(nBlocks);
    if (feeRate == CFeeRate(0))
        return -1.0;

    return ValueFromAmount(feeRate.GetFeePerK());
}

UniValue estimatepriority(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "estimatepriority nblocks\n"
            "\nEstimates the approximate priority a zero-fee transaction needs to begin\n"
            "confirmation within nblocks blocks.\n"
            "\nArguments:\n"
            "1. nblocks     (numeric)\n"
            "\nResult:\n"
            "n              (numeric) estimated priority\n"
            "\n"
            "A negative value is returned if not enough transactions and blocks\n"
            "have been observed to make an estimate.\n"
            "\nExample:\n"
            + HelpExampleCli("estimatepriority", "6")
            );

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    return mempool.estimatePriority(nBlocks);
}

UniValue estimatesmartfee(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "estimatesmartfee nblocks\n"
            "\nWARNING: This interface is unstable and may disappear or change!\n"
            "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
            "confirmation within nblocks blocks if possible and return the number of blocks\n"
            "for which the estimate is valid.\n"
            "\nArguments:\n"
            "1. nblocks     (numeric)\n"
            "\nResult:\n"
            "{\n"
            "  \"feerate\" : x.x,     (numeric) estimate fee-per-kilobyte (in BTC)\n"
            "  \"blocks\" : n         (numeric) block number where estimate was found\n"
            "}\n"
            "\n"
            "A negative value is returned if not enough transactions and blocks\n"
            "have been observed to make an estimate for any number of blocks.\n"
            "However it will not return a value below the mempool reject fee.\n"
            "\nExample:\n"
            + HelpExampleCli("estimatesmartfee", "6")
            );

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = params[0].get_int();

    UniValue result(UniValue::VOBJ);
    int answerFound;
    CFeeRate feeRate = mempool.estimateSmartFee(nBlocks, &answerFound);
    result.pushKV("feerate", feeRate == CFeeRate(0) ? -1.0 : ValueFromAmount(feeRate.GetFeePerK()));
    result.pushKV("blocks", answerFound);
    return result;
}

UniValue estimatesmartpriority(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "estimatesmartpriority nblocks\n"
            "\nWARNING: This interface is unstable and may disappear or change!\n"
            "\nEstimates the approximate priority a zero-fee transaction needs to begin\n"
            "confirmation within nblocks blocks if possible and return the number of blocks\n"
            "for which the estimate is valid.\n"
            "\nArguments:\n"
            "1. nblocks     (numeric)\n"
            "\nResult:\n"
            "{\n"
            "  \"priority\" : x.x,    (numeric) estimated priority\n"
            "  \"blocks\" : n         (numeric) block number where estimate was found\n"
            "}\n"
            "\n"
            "A negative value is returned if not enough transactions and blocks\n"
            "have been observed to make an estimate for any number of blocks.\n"
            "However if the mempool reject fee is set it will return 1e9 * MAX_MONEY.\n"
            "\nExample:\n"
            + HelpExampleCli("estimatesmartpriority", "6")
            );

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = params[0].get_int();

    UniValue result(UniValue::VOBJ);
    int answerFound;
    double priority = mempool.estimateSmartPriority(nBlocks, &answerFound);
    result.pushKV("priority", priority);
    result.pushKV("blocks", answerFound);
    return result;
}
