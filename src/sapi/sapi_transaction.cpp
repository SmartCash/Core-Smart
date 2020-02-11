// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sapi/sapi_transaction.h"

#include "core_io.h"
#include "coins.h"
#include "consensus/validation.h"
#include "smartnode/instantx.h"
#include "validation.h"

extern void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);

static bool transaction_send(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool transaction_check(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool transaction_create(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);

SAPI::EndpointGroup transactionEndpoints = {
    "transaction",
    {
        {
            "check/{txhash}", HTTPRequest::GET, UniValue::VNULL, transaction_check,
            {

            }
        },
        {
            "send", HTTPRequest::POST, UniValue::VOBJ, transaction_send,
            {
                SAPI::BodyParameter(SAPI::Keys::rawtx, new SAPI::Validation::HexString()),
                SAPI::BodyParameter(SAPI::Keys::instantpay, new SAPI::Validation::Bool(), true),
                SAPI::BodyParameter(SAPI::Keys::overridefees, new SAPI::Validation::Bool(), true)
            }
        },
        {
            "create", HTTPRequest::POST, UniValue::VOBJ, transaction_create,
            {
                SAPI::BodyParameter(SAPI::Keys::inputs, new SAPI::Validation::Transactions()),
                SAPI::BodyParameter(SAPI::Keys::outputs, new SAPI::Validation::Outputs()),
                SAPI::BodyParameter(SAPI::Keys::locktime, new SAPI::Validation::UInt(), true),
            }
        }
    }
};

static bool transaction_check(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    if ( !mapPathParams.count("txhash") )
        return SAPI::Error(req, SAPI::TxNotSpecified, "No hash specified. Use /transaction/check/<txhash>");

    std::string hashStr = mapPathParams.at("txhash");
    uint256 hash;

    if( !ParseHashStr(hashStr, hash) )
        return SAPI::Error(req, SAPI::TxNotSpecified, "Invalid hash specified. Use /transaction/check/<txhash>");

    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, false))
        return SAPI::Error(req, SAPI::TxNotFound, "No information available about the transaction");

    string strHex = EncodeHexTx(tx, SERIALIZE_TRANSACTION_NO_WITNESS);

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", strHex);

    uint256 txid = tx.GetHash();
    result.pushKV("txid", txid.GetHex());
    result.pushKV("size", (int)::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION));
    result.pushKV("version", tx.nVersion);
    result.pushKV("locktime", (int64_t)tx.nLockTime);
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
    result.pushKV("vin", vin);
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
    result.pushKV("vout", vout);

    if (!hashBlock.IsNull()){
        LOCK(cs_main);
        result.pushKV("blockhash", hashBlock.GetHex());
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                result.pushKV("height", pindex->nHeight);
                result.pushKV("confirmations", 1 + chainActive.Height() - pindex->nHeight);
                result.pushKV("blockTime", pindex->GetBlockTime());
            } else {
                result.pushKV("height", -1);
                result.pushKV("confirmations", 0);
            }
        }
    }

    if(instantsend.HasTxLockRequest(tx.GetHash())){

        UniValue instantPay(UniValue::VOBJ);

        int nSignatures = instantsend.GetTransactionLockSignatures(tx.GetHash());
        int nSignaturesMax = CTxLockRequest(tx).GetMaxSignatures();
        bool fResult = instantsend.IsLockedInstantSendTransaction(tx.GetHash());
        bool fTimeout = instantsend.IsTxLockCandidateTimedOut(tx.GetHash());

        instantPay.pushKV("valid", fResult);
        instantPay.pushKV("timedOut", fTimeout);
        instantPay.pushKV("locksReceived", nSignatures);
        instantPay.pushKV("locksMax", nSignaturesMax);

        result.pushKV("instantPay", instantPay);
    }

    SAPI::WriteReply(req, result);

    return true;
}

static bool transaction_send(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{

    LOCK(cs_main);

    std::string rawTx = bodyParameter[SAPI::Keys::rawtx].get_str();
    bool fInstantSend = bodyParameter.exists(SAPI::Keys::instantpay) ? bodyParameter[SAPI::Keys::instantpay].get_bool() : false;
    bool fOverrideFees = bodyParameter.exists(SAPI::Keys::overridefees) ? bodyParameter[SAPI::Keys::overridefees].get_bool() : false;

    // parse hex string from parameter
    CTransaction tx;

    if (!DecodeHexTx(tx, rawTx))
        return SAPI::Error(req, SAPI::TxDecodeFailed, "TX decode failed");

    uint256 hashTx = tx.GetHash();

    CCoinsViewCache &view = *pcoinsTip;
    bool fHaveChain = false;
    for (size_t o = 0; !fHaveChain && o < tx.vout.size(); o++) {
        const Coin& existingCoin = view.AccessCoin(COutPoint(hashTx, o));
        fHaveChain = !existingCoin.IsSpent();
    }
    bool fHaveMempool = mempool.exists(hashTx);
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        if (fInstantSend && !instantsend.ProcessTxLockRequest(tx, *g_connman)) {
            return SAPI::Error(req, SAPI::TxNoValidInstantPay, "Not a valid InstantSend transaction");
        }
        CValidationState state;
        bool fMissingInputs;
        if (!AcceptToMemoryPool(mempool, state, tx, false, &fMissingInputs, false, !fOverrideFees)) {
            if (state.IsInvalid()) {
                return SAPI::Error(req, SAPI::TxRejected, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            } else {
                if (fMissingInputs) {
                    return SAPI::Error(req, SAPI::TxMissingInputs, "Missing inputs");
                }
                return SAPI::Error(req, SAPI::TxRejected, state.GetRejectReason());
            }
        }
    } else if (fHaveChain) {
        return SAPI::Error(req, SAPI::TxAlreadyInBlockchain, "Transaction already in block chain");
    }

    if(!g_connman)
        return SAPI::Error(req, SAPI::TxCantRelay, "Error: Peer-to-peer functionality missing or disabled");

    g_connman->RelayTransaction(tx);

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", hashTx.GetHex());
    SAPI::WriteReply(req, result);

    return true;
}

static bool transaction_create(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    UniValue inputs = bodyParameter[SAPI::Keys::inputs].get_array();
    UniValue sendTo = bodyParameter[SAPI::Keys::outputs].get_obj();
    CMutableTransaction rawTx;

    if (bodyParameter.exists(SAPI::Keys::locktime)) {
        rawTx.nLockTime = bodyParameter[SAPI::Keys::locktime].get_int64();
    }

    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            return SAPI::Error(req, SAPI::TxMissingVout, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            return SAPI::Error(req, SAPI::TxInvalidParameter, "Invalid parameter, vout must be positive");

        uint32_t nSequence = (rawTx.nLockTime ? std::numeric_limits<uint32_t>::max() - 1 : std::numeric_limits<uint32_t>::max());

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max())
                return SAPI::Error(req, SAPI::TxInvalidParameter, "Invalid parameter, sequence number is out of range");
            else
                nSequence = (uint32_t)seqNr64;
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);
        rawTx.vin.push_back(in);
    }

    set<CBitcoinAddress> setAddress;
    vector<string> addrList = sendTo.getKeys();
    BOOST_FOREACH(const string& name_, addrList) {
        if (name_ == "data") {
            std::vector<unsigned char> data = ParseHexV(sendTo[name_].getValStr(),"Data");

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CBitcoinAddress address(name_);
            if (!address.IsValid())
                return SAPI::Error(req, SAPI::TxInvalidParameter, string("Invalid SmartCash address: ") + name_);

            if (setAddress.count(address))
                return SAPI::Error(req, SAPI::TxInvalidParameter, string("Invalid parameter, duplicated address: ") + name_);
            setAddress.insert(address);

            CScript scriptPubKey = GetScriptForDestination(address.Get());
            CAmount nAmount = AmountFromValue(sendTo[name_]);

            CTxOut out(nAmount, scriptPubKey);
            rawTx.vout.push_back(out);
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(rawTx));
    SAPI::WriteReply(req, result);

    return true;
}
