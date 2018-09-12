// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sapi.h"

#include "checkpoints.h"

using namespace std;

static bool blockchain_info(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);
static bool blockchain_height(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);
static bool blockchain_block(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);

std::vector<SAPI::Endpoint> blockchainEndpoints = {
    {"", HTTPRequest::GET, UniValue::VNULL, blockchain_info, {}},
    {"/height", HTTPRequest::GET, UniValue::VNULL, blockchain_height, {}},
    {"/block", HTTPRequest::GET, UniValue::VNULL, blockchain_block, {}}
};

bool SAPIBlockchain(HTTPRequest* req, const std::string& strURIPart)
{
    return SAPIExecuteEndpoint(req, strURIPart, blockchainEndpoints);
}

static bool blockchain_info(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
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

    SAPIWriteReply(req, obj);

    return true;
}

static bool blockchain_height(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
{
    LOCK(cs_main);

    string strHeight = std::to_string(chainActive.Height());
    SAPIWriteReply(req, strHeight);

    return true;
}

static bool blockchain_block(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
{
    if ( strURIPart.length() <= 1 || strURIPart == "/" )
        return SAPI::Error(req, SAPI::BlockNotSpecified, "No height or hash specified. Use /blockchain/block/<height or hash>");

    std::string blockStr = strURIPart.substr(1);
    uint256 hash;

    LOCK(cs_main);

    if( IsInteger(blockStr) ){

        int64_t nHeight;

        if( !ParseInt64(blockStr, &nHeight) )
            return SAPI::Error(req, SAPI::UIntOverflow, "Integer overflow.");

        if ( nHeight < 0 ||  nHeight > chainActive.Height() )
            return SAPI::Error(req, SAPI::BlockHeightOutOfRange, "Block height out of range");

        CBlockIndex* pblockindex = chainActive[nHeight];
        hash = pblockindex->GetBlockHash();
    }else if( !ParseHashStr(blockStr, hash) ){
        return SAPI::Error(req, SAPI::BlockNotSpecified, "No height or hash specified. Use /blockchain/block/<height or hash>");
    }

    if (mapBlockIndex.count(hash) == 0)
        return SAPI::Error(req, SAPI::BlockNotFound, "Block not found");

    CBlock block;
    CBlockIndex* blockindex = mapBlockIndex[hash];

    if (fHavePruned && !(blockindex->nStatus & BLOCK_HAVE_DATA) && blockindex->nTx > 0)
        return SAPI::Error(req, SAPI::BlockNotFound, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, blockindex, Params().GetConsensus()))
        return SAPI::Error(req, SAPI::BlockNotFound, "Can't read block from disk");

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

    SAPIWriteReply(req, result);

    return true;
}

