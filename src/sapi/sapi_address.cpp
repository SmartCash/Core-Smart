// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sapi.h"
#include "rpc/client.h"
#include "sapi_validation.h"
#include "sapi/sapi_address.h"
#include "txdb.h"
#include "validation.h"

using namespace std;
using namespace SAPI;

struct CAddressBalance
{
    std::string address;
    CAmount balance;
    CAmount received;

    CAddressBalance(std::string address, CAmount balance, CAmount received) :
        address(address), balance(balance), received(received){}
};


bool amountSort(std::pair<CAddressUnspentKey, CAddressUnspentValue> a,
                std::pair<CAddressUnspentKey, CAddressUnspentValue> b)
{
    return a.second.satoshis > b.second.satoshis;
}

bool spendingSort(std::pair<CAddressIndexKey, CAmount> a,
                std::pair<CAddressIndexKey, CAmount> b) {
    return a.first.spending != b.first.spending;
}

bool timestampSort(const CSAPIDeposit& a,
                   const CSAPIDeposit& b) {
    return a.GetTimestamp() < b.GetTimestamp();
}

static bool address_balance(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);
static bool address_balances(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);
static bool address_deposit(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);
static bool address_utxos(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);
static bool address_utxos_amount(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);


std::vector<Endpoint> addressEndpoints = {
    {
        "/balance", HTTPRequest::GET, UniValue::VNULL, address_balance,
        {
            // No body parameter
        }
    },
    {
        "/balances", HTTPRequest::POST, UniValue::VARR, address_balances,
        {
            // No body parameter
        }
    },
    {
        "/deposit", HTTPRequest::POST, UniValue::VOBJ, address_deposit,
        {
            BodyParameter(Keys::address,        new SAPI::Validation::SmartCashAddress()),
            BodyParameter(Keys::timestampFrom,  new SAPI::Validation::Int()),
            BodyParameter(Keys::timestampTo,    new SAPI::Validation::Int()),
            BodyParameter(Keys::pageNumber,     new SAPI::Validation::IntRange(1,INT_MAX)),
            BodyParameter(Keys::pageSize,       new SAPI::Validation::IntRange(1,1000))
        }
    },
    {
        "/unspent", HTTPRequest::GET, UniValue::VNULL, address_utxos,
        {
            // No body parameter
        }
    },
    {
        "/unspent/amount", HTTPRequest::POST, UniValue::VOBJ, address_utxos_amount,
        {
            BodyParameter(Keys::address, new SAPI::Validation::SmartCashAddress()),
            BodyParameter(Keys::amount,  new SAPI::Validation::DoubleRange(1.0 / COIN,(double)MAX_MONEY / COIN)),
        }
    }
};

bool SAPIAddress(HTTPRequest* req, const std::string& strURIPart)
{
    return SAPIExecuteEndpoint(req, strURIPart, addressEndpoints);
}

static bool GetAddressesBalances(HTTPRequest* req, std::vector<std::string> vecAddr, std::vector<CAddressBalance> &vecBalances)
{
    SAPI::Codes code = SAPI::Valid;
    std::string error = std::string();
    std::vector<SAPI::Result> errors;

    vecBalances.clear();

    for( auto addrStr : vecAddr ){

        CBitcoinAddress address(addrStr);
        uint160 hashBytes;
        int type = 0;

        if (!address.GetIndexKey(hashBytes, type)) {
            code = SAPI::InvalidSmartCashAddress;
            std::string message = "Invalid address: " + addrStr;
            errors.push_back(SAPI::Result(code, message));
            continue;
        }

        std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;

        if (!GetAddressIndex(hashBytes, type, addressIndex)) {
            code = SAPI::AddressNotFound;
            std::string message = "No information available for " + addrStr;
            errors.push_back(SAPI::Result(code, message));
            continue;
        }

        CAmount balance = 0;
        CAmount received = 0;

        for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin(); it!=addressIndex.end(); it++) {
            if (it->second > 0) {
                received += it->second;
            }
            balance += it->second;
        }

        vecBalances.push_back(CAddressBalance(addrStr, balance, received));
    }

    if( errors.size() ){
        return Error(req, HTTP_BAD_REQUEST, errors);
    }

    if( !vecBalances.size() ){
        return Error(req, HTTP_INTERNAL_SERVER_ERROR, "Balance check failed unexpected.");
    }

    return true;
}

static bool GetUTXOs(HTTPRequest* req, const CBitcoinAddress& address, std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >& utxos){

    uint160 hashBytes;
    int type = 0;

    if (!address.GetIndexKey(hashBytes, type)) {
        return Error(req, SAPI::InvalidSmartCashAddress, "Invalid address");
    }

    if (!GetAddressUnspent(hashBytes, type, utxos)) {
        return Error(req, SAPI::AddressNotFound, "No information available for address");
    }

    return true;
}

static bool GetBlockInfoForTimestamp(const unsigned int &timestamp, uint256 &blockHash)
{
    bool fTimestampIndex = GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX);

    if (!fTimestampIndex)
        return error("Timestamp index not enabled");

    if (!pblocktree->ReadTimestampIndex(timestamp, blockHash))
        return error("Unable to get hashes for timestamps");

    return true;
}

inline double CalculateFee( int nInputs )
{
    double feeCalc = (((nInputs * 148) + (2 * 34) + 10 + 9) / 1024.0) * 0.001;
    return std::max(std::round( feeCalc * 1000.0 ) / 1000.0, 0.001);
}

static bool address_balance(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
{

    if ( strURIPart.length() <= 1 || strURIPart == "/" )
        return Error(req, HTTP_BAD_REQUEST, "No SmartCash address specified. Use /address/balance/<smartcash_address>");

    std::string addrStr = strURIPart.substr(1);
    std::vector<CAddressBalance> vecResult;

    if( !GetAddressesBalances(req, {addrStr},vecResult) )
        return false;

    CAddressBalance result = vecResult.front();

    UniValue response(UniValue::VOBJ);
    response.pushKV(Keys::address, result.address);
    response.pushKV("received", CAmountToDouble(result.received));
    response.pushKV("sent", CAmountToDouble(result.received - result.balance));
    response.pushKV("balance", CAmountToDouble(result.balance));

    SAPIWriteReply(req, response);

    return true;
}


static bool address_balances(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
{

    if( !bodyParameter.isArray() || bodyParameter.empty() )
        return Error(req, HTTP_BAD_REQUEST, "Addresses are expedted to be a JSON array: [ \"address\", ... ]");

    std::vector<CAddressBalance> vecResult;
    std::vector<std::string> vecAddresses;

    for( auto addr : bodyParameter.getValues() ){

        std::string addrStr = addr.get_str();

        if( std::find(vecAddresses.begin(), vecAddresses.end(), addrStr) == vecAddresses.end() )
            vecAddresses.push_back(addrStr);
    }

    if( !GetAddressesBalances(req, vecAddresses, vecResult) )
            return false;

    UniValue response(UniValue::VARR);

    for( auto result : vecResult ){

        UniValue entry(UniValue::VOBJ);
        entry.pushKV(Keys::address, result.address);
        entry.pushKV("received", CAmountToDouble(result.received));
        entry.pushKV("sent", CAmountToDouble(result.received - result.balance));
        entry.pushKV("balance", CAmountToDouble(result.balance));
        response.push_back(entry);
    }

    SAPIWriteReply(req, response);

    return true;
}

static bool address_deposit(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
{
    int64_t nTime0, nTime1, nTime2, nTime3, nTime4, nTime5;

    nTime0 = GetTimeMicros();

    int64_t start = bodyParameter[Keys::timestampFrom].get_int64();
    int64_t end = bodyParameter[Keys::timestampTo].get_int64();
    int nPageNumber = bodyParameter[Keys::pageNumber].get_int64();
    int nPageSize = bodyParameter[Keys::pageSize].get_int64();

    if ( start && end && end <= start)
        return Error(req, HTTP_BAD_REQUEST, "\"" + Keys::timestampFrom + "\" is expected to be greater than \"" + Keys::timestampTo + "\"");

    std::string addrStr = bodyParameter[Keys::address].get_str();
    CBitcoinAddress address(addrStr);
    uint160 hashBytes;
    int type = 0;
    int nDeposits = 0;

    if (!address.GetIndexKey(hashBytes, type))
        return Error(req, HTTP_BAD_REQUEST,"Invalid address: " + addrStr);

    std::vector<std::pair<CDepositIndexKey, CDepositValue> > depositIndex;

    nTime1 = GetTimeMicros();

    if (!GetDepositIndexCount(hashBytes, type, nDeposits, start, end) )
        return Error(req, HTTP_BAD_REQUEST, "No information available for the provided timerange.");

    if (!nDeposits)
        return Error(req, SAPI::NoDepositAvailble, "No deposits available for the given timerange.");

    int nPages = nDeposits / nPageSize;
    if( nDeposits % nPageSize ) nPages++;

    if (nPageNumber > nPages)
        return Error(req, SAPI::PageOutOfRange, strprintf("Page number out of range: 1 - %d", nPages));

    int nDepositStart = ( nPageNumber - 1 ) * nPageSize;
    int nLimit = (nDeposits % nPageSize) && nPageNumber == nPages ? (nDeposits % nPageSize) : nPageSize;

    nTime2 = GetTimeMicros();

    if (!GetDepositIndex(hashBytes, type, depositIndex, start, nDepositStart , nLimit))
        return Error(req, HTTP_BAD_REQUEST, "No information available for " + addrStr);

    nTime3 = GetTimeMicros();

    UniValue result(UniValue::VOBJ);

    UniValue arrDeposit(UniValue::VARR);

    for (std::vector<std::pair<CDepositIndexKey, CDepositValue> >::const_iterator it=depositIndex.begin();
         it!=depositIndex.end();
         it++) {

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("txhash", it->first.txhash.GetHex());
        obj.pushKV("blockHeight", it->second.blockHeight);
        obj.pushKV("timestamp", (int64_t)it->first.timestamp);
        obj.pushKV("amount", CAmountToDouble(it->second.satoshis));

        arrDeposit.push_back(obj);
    }

    UniValue obj(UniValue::VOBJ);

    obj.pushKV("page", nPageNumber);
    obj.pushKV("pages", nPages);
    obj.pushKV("deposits",arrDeposit);

    nTime4 = GetTimeMicros();

    SAPIWriteReply(req, obj);

    nTime5 = GetTimeMicros();

    LogPrint("sapi-benchmark", "address_deposit\n");
    LogPrint("sapi-benchmark", " Prepare parameter: %.2fms\n", (nTime1 - nTime0) * 0.001);
    LogPrint("sapi-benchmark", " Get deposit count: %.2fms\n", (nTime2 - nTime1) * 0.001);
    LogPrint("sapi-benchmark", " Get deposit index: %.2fms\n", (nTime3 - nTime2) * 0.001);
    LogPrint("sapi-benchmark", " Process deposits: %.2fms\n", (nTime4 - nTime3) * 0.001);
    LogPrint("sapi-benchmark", " Write reply: %.2fms\n", (nTime5 - nTime4) * 0.001);
    LogPrint("sapi-benchmark", " Total: %.2fms\n\n", (nTime5 - nTime0) * 0.001);

    return true;
}

static bool address_utxos(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
{
    int64_t nTime0, nTime1, nTime2, nTime3, nTime4, nTime5;

    nTime0 = GetTimeMicros();

    if ( strURIPart.length() <= 1 || strURIPart == "/" )
        return Error(req, HTTP_BAD_REQUEST, "No SmartCash address specified. Use /address/unspent/<smartcash_address>");

    std::string addrStr = strURIPart.substr(1);
    CBitcoinAddress address(addrStr);
    int64_t nHeight = chainActive.Height();
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    nTime1 = GetTimeMicros();

    if( !GetUTXOs(req, address, unspentOutputs ) )
        return false;

    nTime2 = GetTimeMicros();

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), heightSort);

    nTime3 = GetTimeMicros();

    UniValue result(UniValue::VARR);

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++) {
        UniValue output(UniValue::VOBJ);
        std::string address;

        CSpentIndexValue spentInfo;
        CSpentIndexKey spentKey(it->first.txhash, (int)it->first.index);

        // Ignore inputs currently used for tx in the mempool
        if (mempool.getSpentIndex(spentKey, spentInfo))
            continue;

        output.pushKV("txid", it->first.txhash.GetHex());
        output.pushKV("index", (int)it->first.index);
        output.pushKV(Keys::address, address);
        output.pushKV("script", HexStr(it->second.script.begin(), it->second.script.end()));
        output.pushKV("value", CAmountToDouble(it->second.satoshis));
        output.pushKV("confirmations", nHeight - it->second.blockHeight);
        result.push_back(output);
    }

    nTime4 = GetTimeMicros();

    SAPIWriteReply(req, result);

    nTime5 = GetTimeMicros();

    LogPrint("sapi-benchmark", "\naddress_utxos\n");
    LogPrint("sapi-benchmark", " Query utxos: %.2fms\n", (nTime2 - nTime1) * 0.001);
    LogPrint("sapi-benchmark", " Sort utxos: %.2fms\n", (nTime3 - nTime2) * 0.001);
    LogPrint("sapi-benchmark", " Process utxos: %.2fms\n", (nTime4 - nTime3) * 0.001);
    LogPrint("sapi-benchmark", " Write reply: %.2fms\n", (nTime5 - nTime4) * 0.001);
    LogPrint("sapi-benchmark", " Total: %.2fms\n\n", (nTime5 - nTime0) * 0.001);


    return true;
}

static bool address_utxos_amount(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
{
    int64_t nTime0, nTime1, nTime2, nTime3, nTime4, nTime5;

    nTime0 = GetTimeMicros();

    std::string addrStr = bodyParameter[SAPI::Keys::address].get_str();
    double expectedAmount = bodyParameter[SAPI::Keys::amount].get_real();

    CBitcoinAddress address(addrStr);
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    nTime1 = GetTimeMicros();

    if( !GetUTXOs(req, address, unspentOutputs ) )
        return false;

    nTime2 = GetTimeMicros();

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), amountSort);

    nTime3 = GetTimeMicros();

    int64_t nHeight = chainActive.Height();
    double fee = CalculateFee(0);
    double amountSum = 0.0;

    UniValue inputs(UniValue::VARR);

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++) {
        UniValue output(UniValue::VOBJ);

        CSpentIndexValue spentInfo;
        CSpentIndexKey spentKey(it->first.txhash, (int)it->first.index);

        // Ignore inputs currently used for tx in the mempool
        if (mempool.getSpentIndex(spentKey, spentInfo))
            continue;

        //Ignore inputs that are not valid for instantpay
        if( (nHeight - it->second.blockHeight + 1) < 2 )
            continue;

        double amount = CAmountToDouble(it->second.satoshis);
        amountSum += amount;

        UniValue obj(UniValue::VOBJ);

        obj.pushKV("txid", it->first.txhash.GetHex());
        obj.pushKV("index", (int64_t)it->first.index);
        obj.pushKV("confirmations", nHeight - it->second.blockHeight + 1);
        obj.pushKV("amount", amount);

        inputs.push_back(obj);

        fee = CalculateFee(inputs.size());

        if( amountSum > expectedAmount + fee )
            break;
    }

    if( amountSum < expectedAmount + fee )
        return Error(req, HTTP_BAD_REQUEST, "Amount exceeds balance.");


    UniValue result(UniValue::VOBJ);
    CScript script = GetScriptForDestination(address.Get());

    result.pushKV("blockHeight", nHeight);
    result.pushKV("fee", fee);
    result.pushKV("scriptPubKey", HexStr(script.begin(), script.end()));
    result.pushKV("address", addrStr);
    result.pushKV("inputsAmount", amountSum);
    result.pushKV("change", amountSum - expectedAmount - fee);
    result.pushKV("inputs", inputs);

    nTime4 = GetTimeMicros();

    SAPIWriteReply(req, result);

    nTime5 = GetTimeMicros();

    LogPrint("sapi-benchmark", "\naddress_utxos\n");
    LogPrint("sapi-benchmark", " Query utxos: %.2fms\n", (nTime2 - nTime1) * 0.001);
    LogPrint("sapi-benchmark", " Sort utxos: %.2fms\n", (nTime3 - nTime2) * 0.001);
    LogPrint("sapi-benchmark", " Evaluate inputs: %.2fms\n", (nTime4 - nTime3) * 0.001);
    LogPrint("sapi-benchmark", " Write reply: %.2fms\n", (nTime5 - nTime4) * 0.001);
    LogPrint("sapi-benchmark", " Total: %.2fms\n\n", (nTime5 - nTime0) * 0.001);

    return true;
}
