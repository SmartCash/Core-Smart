// Copyright (c) 2017 - 2020 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "core_io.h"
#include "sapi.h"
#include "consensus/validation.h"
#include "smartnode/instantx.h"
#include "validation.h"
#include "checkpoints.h"

#define BLOCKS_API_MAX_COUNT        10
#define TRANSACTIONS_API_MAX_COUNT  10

extern void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);

static bool blockchain_info(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool blockchain_height(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool blockchain_block(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool blockchain_block_transactions(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool blockchain_blocks_latest(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool blockchain_blocks_range(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool blockchain_transactions_latest(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);

SAPI::EndpointGroup blockchainEndpoints = {
    "blockchain",
    {
        {"", HTTPRequest::GET, UniValue::VNULL, blockchain_info, {}},
        {"height", HTTPRequest::GET, UniValue::VNULL, blockchain_height, {}},
        {"block/{blockinfo}", HTTPRequest::GET, UniValue::VNULL, blockchain_block, {}},
        {"block/transactions", HTTPRequest::POST, UniValue::VOBJ, blockchain_block_transactions,
         {
             SAPI::BodyParameter(SAPI::Keys::hash,           new SAPI::Validation::HexString(), true),
             SAPI::BodyParameter(SAPI::Keys::height,         new SAPI::Validation::UInt(), true),
             SAPI::BodyParameter(SAPI::Keys::pageNumber,     new SAPI::Validation::IntRange(1,INT_MAX)),
             SAPI::BodyParameter(SAPI::Keys::pageSize,       new SAPI::Validation::IntRange(1,100))
         }
        },
        {"blocks/latest/{count}", HTTPRequest::GET, UniValue::VNULL, blockchain_blocks_latest, {}},
        {"blocks/{from}/{to}", HTTPRequest::GET, UniValue::VNULL, blockchain_blocks_range, {}},
        {"transactions/latest/{count}", HTTPRequest::GET, UniValue::VNULL, blockchain_transactions_latest, {}}
    }
};

static UniValue GetBlockInfo(CBlockIndex *blockindex, const CBlock &block)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("strippedsize", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS)));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("weight", (int)::GetBlockWeight(block)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", block.nVersion)));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    UniValue txs(UniValue::VARR);
    BOOST_FOREACH(const CTransaction&tx, block.vtx)
    {
            txs.push_back(tx.GetHash().GetHex());
    }
    result.push_back(Pair("tx", txs));
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));

    return result;
}

static bool GetTransactionInfo(HTTPRequest* req, uint256 nHash, const CTransaction &tx, UniValue &txObj)
{
    string strHex = EncodeHexTx(tx, SERIALIZE_TRANSACTION_NO_WITNESS);
    txObj.pushKV("hex", strHex);

    uint256 txid = tx.GetHash();
    txObj.pushKV("txid", txid.GetHex());
    txObj.pushKV("size", (int)::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION));
    txObj.pushKV("version", tx.nVersion);
    txObj.pushKV("locktime", (int64_t)tx.nLockTime);
    UniValue vin(UniValue::VARR);
    BOOST_FOREACH(const CTxIn& txin, tx.vin) {

        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.pushKV("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
        else {

            CTransaction txInput;
            uint256 hashBlockIn;
            if (!GetTransaction(txin.prevout.hash, txInput, Params().GetConsensus(), hashBlockIn, false))
                return SAPI::Error(req, SAPI::TxNotFound, "No information available about one of the inputs.");

            const CTxOut& txout = txInput.vout[txin.prevout.n];

            in.pushKV("txid", txin.prevout.hash.GetHex());
            in.pushKV("value", ValueFromAmount(txout.nValue));
            in.pushKV("n", (int64_t)txin.prevout.n);
            UniValue o(UniValue::VOBJ);
            ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
            in.pushKV("scriptPubKey", o);
        }

        in.pushKV("sequence", (int64_t)txin.nSequence);
        vin.push_back(in);
    }
    txObj.pushKV("vin", vin);
    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];
        UniValue out(UniValue::VOBJ);
        out.pushKV("value", ValueFromAmount(txout.nValue));
        out.pushKV("n", (int64_t)i);
        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        out.pushKV("scriptPubKey", o);
        vout.push_back(out);
    }
    txObj.pushKV("vout", vout);

    if (!nHash.IsNull()) {
        txObj.pushKV("blockhash", nHash.GetHex());
        BlockMap::iterator mi = mapBlockIndex.find(nHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                txObj.pushKV("height", pindex->nHeight);
                txObj.pushKV("confirmations", 1 + chainActive.Height() - pindex->nHeight);
                txObj.pushKV("blockTime", pindex->GetBlockTime());
            } else {
                txObj.pushKV("height", -1);
                txObj.pushKV("confirmations", 0);
            }
        }
    }

    return true;
}

static bool blockchain_info(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    UniValue obj(UniValue::VOBJ);

    {
        LOCK(cs_main);
        obj.push_back(Pair("chain",                 Params().NetworkIDString()));
        obj.push_back(Pair("blocks",                (int)chainActive.Height()));
        obj.push_back(Pair("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1));
        obj.push_back(Pair("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex()));
        obj.push_back(Pair("difficulty",            (double)GetDifficulty()));
        obj.push_back(Pair("mediantime",            (int64_t)chainActive.Tip()->GetMedianTimePast()));
        obj.push_back(Pair("verificationprogress",  Checkpoints::GuessVerificationProgress(Params().Checkpoints(), chainActive.Tip())));
        obj.push_back(Pair("chainwork",             chainActive.Tip()->nChainWork.GetHex()));
    }

    SAPI::WriteReply(req, obj);

    return true;
}

static bool blockchain_height(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    LOCK(cs_main);

    UniValue result(UniValue::VOBJ);
    result.pushKV("height", chainActive.Height());
    SAPI::WriteReply(req, result);

    return true;
}

static bool blockchain_block(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    if ( !mapPathParams.count("blockinfo") )
        return SAPI::Error(req, SAPI::BlockNotSpecified, "No height or hash specified. Use /blockchain/block/<height or hash>");

    std::string blockInfoStr = mapPathParams.at("blockinfo");
    uint256 hash;

    LOCK(cs_main);

    if( IsInteger(blockInfoStr) ){

        int64_t nHeight;

        if( !ParseInt64(blockInfoStr, &nHeight) )
            return SAPI::Error(req, SAPI::UIntOverflow, "Integer overflow.");

        if ( nHeight < 0 ||  nHeight > chainActive.Height() )
            return SAPI::Error(req, SAPI::BlockHeightOutOfRange, "Block height out of range");

        CBlockIndex* pblockindex = chainActive[nHeight];
        hash = pblockindex->GetBlockHash();
    }else if( !ParseHashStr(blockInfoStr, hash) ){
        return SAPI::Error(req, SAPI::BlockNotSpecified, "No valid height or hash specified. Use /blockchain/block/<height or hash>");
    }

    if (mapBlockIndex.count(hash) == 0)
        return SAPI::Error(req, SAPI::BlockNotFound, "Block not found");

    CBlock block;
    CBlockIndex* blockindex = mapBlockIndex[hash];

    if (fHavePruned && !(blockindex->nStatus & BLOCK_HAVE_DATA) && blockindex->nTx > 0)
        return SAPI::Error(req, SAPI::BlockNotFound, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, blockindex, Params().GetConsensus()))
        return SAPI::Error(req, SAPI::BlockNotFound, "Can't read block from disk");

    UniValue result = GetBlockInfo(blockindex, block);

    SAPI::WriteReply(req, result);

    return true;
}

static bool blockchain_block_transactions(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{

    uint256 nHash;
    bool fByHash = bodyParameter.exists(SAPI::Keys::hash);
    bool fByHeight = bodyParameter.exists(SAPI::Keys::height);

    if( fByHash && fByHeight ){
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "Both, hash and height are given but only one is allowed. Use either 'hash' or 'height' as parameter in the body.");
    }else if( !fByHash && !fByHeight ){
        return SAPI::Error(req, SAPI::BlockNotSpecified, "No valid height or hash specified: Use either 'hash' or 'height' as parameter in the body.");
    }else if( fByHeight ){

        int64_t nHeight = bodyParameter[SAPI::Keys::height].get_int64();

        if ( nHeight < 0 ||  nHeight > chainActive.Height() )
            return SAPI::Error(req, SAPI::BlockHeightOutOfRange, "Block height out of range.");

        CBlockIndex* pblockindex = chainActive[nHeight];
        nHash = pblockindex->GetBlockHash();

    }else if( !ParseHashStr(bodyParameter[SAPI::Keys::hash].get_str(), nHash) ){
        return SAPI::Error(req, SAPI::BlockHashInvalid, "Invalid block hash provided.");
    }

    int64_t nPageNumber = bodyParameter[SAPI::Keys::pageNumber].get_int64();
    int64_t nPageSize = bodyParameter[SAPI::Keys::pageSize].get_int64();

    LOCK(cs_main);

    CBlock block;
    CBlockIndex* blockindex = mapBlockIndex[nHash];

    if (fHavePruned && !(blockindex->nStatus & BLOCK_HAVE_DATA) && blockindex->nTx > 0)
        return SAPI::Error(req, SAPI::BlockNotFound, "Block not available (pruned data).");

    if(!ReadBlockFromDisk(block, blockindex, Params().GetConsensus()))
        return SAPI::Error(req, SAPI::BlockNotFound, "Can't read block from disk.");

    int nTxCount = block.vtx.size();
    int nPages = nTxCount / nPageSize;
    if( nTxCount % nPageSize ) nPages++;

    if (nPageNumber > nPages)
        return SAPI::Error(req, SAPI::PageOutOfRange, strprintf("Page number out of range: 1 - %d.", nPages));


    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("strippedsize", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS)));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("weight", (int)::GetBlockWeight(block)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", block.nVersion)));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));

    UniValue txs(UniValue::VARR);

    int nIndexOffset = static_cast<int>(( nPageNumber - 1 ) * nPageSize);
    auto tx = block.vtx.begin() + nIndexOffset;

    while(tx != block.vtx.end() && static_cast<int64_t>(txs.size()) < nPageSize )
    {
        UniValue txObj(UniValue::VOBJ);
        if (!GetTransactionInfo(req, nHash, *tx, txObj)) {
            return false;
        }
        txs.push_back(txObj);
        ++tx;
    }

    UniValue transactions(UniValue::VOBJ);

    transactions.pushKV("count",static_cast<int64_t>(block.vtx.size()));
    transactions.pushKV("pages", nPages);
    transactions.pushKV("page", nPageNumber);
    transactions.pushKV("data", txs);

    result.push_back(Pair("transactions", transactions));
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));

    SAPI::WriteReply(req, result);

    return true;
}

bool blockchain_blocks_latest(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    int64_t count = BLOCKS_API_MAX_COUNT;

    if (mapPathParams.count("count")) {
        std::string blockInfoStr = mapPathParams.at("count");
        if (IsInteger(blockInfoStr)) {
            if (ParseInt64(blockInfoStr, &count)) {
                count = count > BLOCKS_API_MAX_COUNT ? BLOCKS_API_MAX_COUNT : count;
            }
        }
    }

    UniValue response(UniValue::VARR);

    LOCK(cs_main);

    int64_t currentHeight = chainActive.Height();
    if (currentHeight < count) {
        count = currentHeight;
    }

    for (int i = 0; i < count; i++) {
        CBlock block;
        CBlockIndex* blockindex = chainActive[currentHeight - i];

        if (fHavePruned && !(blockindex->nStatus & BLOCK_HAVE_DATA) && blockindex->nTx > 0)
            return SAPI::Error(req, SAPI::BlockNotFound, "Block not available (pruned data).");

        if(!ReadBlockFromDisk(block, blockindex, Params().GetConsensus()))
            return SAPI::Error(req, SAPI::BlockNotFound, "Can't read block from disk.");

        UniValue blockInfo = GetBlockInfo(blockindex, block);
        response.push_back(blockInfo);
    }

    SAPI::WriteReply(req, response);

    return true;
}

bool blockchain_blocks_range(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    UniValue response(UniValue::VARR);

    LOCK(cs_main);

    int64_t to = chainActive.Height();
    int64_t from = to - BLOCKS_API_MAX_COUNT + 1;

    if (mapPathParams.count("from")) {
        std::string fromStr = mapPathParams.at("from");
        if (IsInteger(fromStr)) {
            if (ParseInt64(fromStr, &from)) {
                from = from < 0 ? 0 : from;
            }
        }
    }

    if (mapPathParams.count("to")) {
        std::string toStr = mapPathParams.at("to");
        if (IsInteger(toStr)) {
            if (ParseInt64(toStr, &to)) {
                to = to > chainActive.Height() ? chainActive.Height() : to;
            }
        }
    }

    // Check that 'from' and 'to' were given in the right order
    if (from >= to) {
        return SAPI::Error(req, SAPI::BlockHeightOutOfRange, "Range should be in ascending order");
    }

    // If number of requested blocks is bigger than max, reduce the range
    if ((to - from + 1) > BLOCKS_API_MAX_COUNT) {
        to = from + BLOCKS_API_MAX_COUNT - 1;
    }

    // Clip range if out of bounds
    from = from < 0 ? 0 : from;
    to = to > chainActive.Height() ? chainActive.Height() : to;

    for (int i = to; i >= from; i--) {
        CBlock block;
        CBlockIndex* blockindex = chainActive[i];

        if (fHavePruned && !(blockindex->nStatus & BLOCK_HAVE_DATA) && blockindex->nTx > 0)
            return SAPI::Error(req, SAPI::BlockNotFound, "Block not available (pruned data).");

        if(!ReadBlockFromDisk(block, blockindex, Params().GetConsensus()))
            return SAPI::Error(req, SAPI::BlockNotFound, "Can't read block from disk.");

        UniValue blockInfo = GetBlockInfo(blockindex, block);
        response.push_back(blockInfo);
    }

    SAPI::WriteReply(req, response);

    return true;
}

bool blockchain_transactions_latest(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    int64_t count = TRANSACTIONS_API_MAX_COUNT;

    if (mapPathParams.count("count")) {
        std::string blockInfoStr = mapPathParams.at("count");
        if (IsInteger(blockInfoStr)) {
            if (ParseInt64(blockInfoStr, &count)) {
                count = count > TRANSACTIONS_API_MAX_COUNT ? TRANSACTIONS_API_MAX_COUNT : count;
            }
        }
    }

    UniValue response(UniValue::VARR);

    LOCK(cs_main);

    int64_t nHeight = chainActive.Height();
    int64_t numTxs = count;

    while (numTxs) {
        CBlock block;
        CBlockIndex* blockindex = chainActive[nHeight];

        if (fHavePruned && !(blockindex->nStatus & BLOCK_HAVE_DATA) && blockindex->nTx > 0)
            return SAPI::Error(req, SAPI::BlockNotFound, "Block not available (pruned data).");

        if (!ReadBlockFromDisk(block, blockindex, Params().GetConsensus()))
            return SAPI::Error(req, SAPI::BlockNotFound, "Can't read block from disk.");

        auto tx = block.vtx.begin();
        while (tx != block.vtx.end() && numTxs) {
            UniValue txObj(UniValue::VOBJ);
            if (!GetTransactionInfo(req, blockindex->GetBlockHash(), *tx, txObj)) {
                return false;
            }
            response.push_back(txObj);
            numTxs--;
        }

        nHeight--;
    }

    SAPI::WriteReply(req, response);

    return true;
}
