// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sapi.h"
#include "rpc/client.h"
#include "sapi_address.h"
#include "sapi_validation.h"

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

bool address_balance(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);
bool address_balances(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);
bool address_deposit(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);
bool address_utxos(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);
bool address_utxos_amount(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter);



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
    std::vector<std::pair<uint160, int> > addresses;
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

    std::vector<std::pair<uint160, int> > addresses;

    uint160 hashBytes;
    int type = 0;
    if (!address.GetIndexKey(hashBytes, type)) {
        return Error(req, SAPI::InvalidSmartCashAddress, "Invalid address");
    }

    addresses.push_back(std::make_pair(hashBytes, type));

    for (std::vector<std::pair<uint160, int> >::iterator it = addresses.begin(); it != addresses.end(); it++) {
        if (!GetAddressUnspent((*it).first, (*it).second, utxos)) {
            return Error(req, SAPI::AddressNotFound, "No information available for address");
        }
    }

    //std::sort(utxos.begin(), utxos.end(), heightSort);

    return true;
}

inline double CalculateFee( int nInputs )
{
    double feeCalc = (((nInputs * 148) + (2 * 34) + 10 + 9) / 1024.0) * 0.001;
    return std::round( feeCalc * 1000.0 ) / 1000.0;
}

bool address_balance(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
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

    string strJSON = response.write(1,1) + "\n";
    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(HTTP_OK, strJSON);

    return true;
}


bool address_balances(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
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

    string strJSON = response.write(2) + "\n";
    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(HTTP_OK, strJSON);

    return true;
}

bool address_deposit(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
{

    int start = bodyParameter[Keys::timestampFrom].get_int64();
    int end = bodyParameter[Keys::timestampTo].get_int64();
    int nPageNumber = bodyParameter[Keys::pageNumber].get_int64();
    int nPageSize = bodyParameter[Keys::pageSize].get_int64();

    if (end < start)
        return Error(req, HTTP_BAD_REQUEST, "\"" + Keys::timestampFrom + "\" is expected to be greater than \"" + Keys::timestampTo + "\"");

    std::vector<std::pair<uint160, int> > addresses;
    std::string addrStr = bodyParameter[Keys::address].get_str();
    CBitcoinAddress address(addrStr);
    uint160 hashBytes;
    int type = 0;

    if (!address.GetIndexKey(hashBytes, type))
        return Error(req, HTTP_BAD_REQUEST,"Invalid address: " + addrStr);

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<uint256> vecBlockHashes;

    CBlockIndex* pIndexStart = nullptr;
    CBlockIndex* pIndexEnd = nullptr;

    if (!GetTimestampIndex(end, start, vecBlockHashes) || vecBlockHashes.size() < 2 )
        return Error(req, HTTP_BAD_REQUEST, "No information available for the given timerange.");

    {
        LOCK(cs_main);

        if (mapBlockIndex.count(vecBlockHashes.front()) == 0)
            return Error(req, HTTP_BAD_REQUEST, "Start block not found");
        if (mapBlockIndex.count(vecBlockHashes.back()) == 0)
            return Error(req, HTTP_BAD_REQUEST, "End block not found");

        pIndexStart = mapBlockIndex[vecBlockHashes.front()];
        pIndexEnd = mapBlockIndex[vecBlockHashes.back()];

    }

    if( !pIndexStart || !pIndexEnd )
        return Error(req, HTTP_BAD_REQUEST, "Could not load block index.");

    if (!GetAddressIndex(hashBytes, type, addressIndex, pIndexStart->nHeight, pIndexEnd->nHeight))
        return Error(req, HTTP_BAD_REQUEST, "No information available for " + addrStr);

    UniValue result(UniValue::VOBJ);

    std::vector<std::pair<CAddressIndexKey, CAmount>> vecDeposits;

    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin();
         it!=addressIndex.end();
         it++) {

        std::string address;

        if (!getAddressFromIndex(it->first.type, it->first.hashBytes, address))
            return Error(req, HTTP_BAD_REQUEST, "Unknown address type");

        // TBD check if its a sent to the address itself
        if( 1 ){
            vecDeposits.push_back(*it);
        }
    }

    UniValue obj(UniValue::VOBJ);
    UniValue arr(UniValue::VARR);
    int nDeposits = vecDeposits.size();
    int nPages = nDeposits / nPageSize;
    if( nDeposits % nPageSize ) nPages++;

    if (nPageNumber > nPages)
        return Error(req, SAPI::PageOutOfRange, strprintf("Page number out of range: 1 - %d", nPages));

    int nDepositStart = ( nPageNumber - 1 ) * nPageSize;

    obj.pushKV("page", nPageNumber);
    obj.pushKV("pages", nPages);

    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin() + nDepositStart;
         it!=addressIndex.end();
         it++) {

        CBlockIndex *pIndex = nullptr;

        {
            LOCK(cs_main);
            pIndex = chainActive[it->first.blockHeight];
        }

        if( !pIndex )
            return Error(req, SAPI::BlockNotFound, "Could not find block index.");

        // Add the deposits to the json response.
        UniValue entry(UniValue::VOBJ);

        entry.pushKV("txhash", HexStr(it->first.txhash.begin(), it->first.txhash.end()));
        entry.pushKV("timestamp", pIndex->GetBlockTime());
        entry.pushKV("amount", CAmountToDouble(it->second));

        arr.push_back(entry);

        if( arr.size() == (size_t)nPageSize ) break;
    }

    obj.pushKV("deposits",arr);

    string strJSON = obj.write(2) + "\n";
    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(HTTP_OK, strJSON);

    return true;
}

bool address_utxos(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
{
    if ( strURIPart.length() <= 1 || strURIPart == "/" )
        return Error(req, HTTP_BAD_REQUEST, "No SmartCash address specified. Use /address/unspent/<smartcash_address>");

    std::string addrStr = strURIPart.substr(1);
    CBitcoinAddress address(addrStr);
    int64_t nHeight = chainActive.Height();
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    if( !GetUTXOs(req, address, unspentOutputs ) )
        return false;

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), heightSort);

    UniValue result(UniValue::VARR);

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++) {
        UniValue output(UniValue::VOBJ);
        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.hashBytes, address)) {
            return Error(req, HTTP_BAD_REQUEST, "Unknown address type");
        }
        output.pushKV("txid", it->first.txhash.GetHex());
        output.pushKV("index", (int)it->first.index);
        output.pushKV(Keys::address, address);
        output.pushKV("script", HexStr(it->second.script.begin(), it->second.script.end()));
        output.pushKV("value", CAmountToDouble(it->second.satoshis));
        output.pushKV("confirmations", nHeight - it->second.blockHeight);
        result.push_back(output);
    }

    string strJSON = result.write(2) + "\n";
    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(HTTP_OK, strJSON);

    return true;
}

bool address_utxos_amount(HTTPRequest* req, const std::string& strURIPart, const UniValue &bodyParameter)
{
    std::string addrStr = bodyParameter[SAPI::Keys::address].get_str();
    double expectedAmount = bodyParameter[SAPI::Keys::amount].get_real();

    CBitcoinAddress address(addrStr);
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    if( !GetUTXOs(req, address, unspentOutputs ) )
        return false;

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), amountSort);

    int64_t nHeight = chainActive.Height();
    double fee = CalculateFee(0);
    double amountSum = 0.0;

    UniValue inputs(UniValue::VARR);

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++) {
        UniValue output(UniValue::VOBJ);
        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.hashBytes, address)) {
            return Error(req, HTTP_BAD_REQUEST, "Unknown address type");
        }

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
    result.pushKV("inputs", inputs);

    string strJSON = result.write(2) + "\n";
    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(HTTP_OK, strJSON);

    return true;
}
