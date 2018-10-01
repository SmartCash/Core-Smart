// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "sapi.h"

#include <algorithm>
#include "base58.h"
#include "rpc/client.h"
#include "sapi_validation.h"
#include "sapi/sapi_address.h"
#include "smartnode/instantx.h"
#include "txdb.h"
#include "random.h"
#include <random>
#include "validation.h"

struct CAddressBalance
{
    std::string address;
    CAmount balance;
    CAmount received;

    CAddressBalance(std::string address, CAmount balance, CAmount received) :
        address(address), balance(balance), received(received){}
};


bool amountSortLTH(std::pair<CAddressUnspentKey, CAddressUnspentValue> a,
                std::pair<CAddressUnspentKey, CAddressUnspentValue> b)
{
    return a.second.satoshis < b.second.satoshis;
}

bool amountSortHTL(std::pair<CAddressUnspentKey, CAddressUnspentValue> a,
                std::pair<CAddressUnspentKey, CAddressUnspentValue> b)
{
    return a.second.satoshis > b.second.satoshis;
}

bool spendingSort(std::pair<CAddressIndexKey, CAmount> a,
                std::pair<CAddressIndexKey, CAmount> b) {
    return a.first.spending != b.first.spending;
}

static bool address_balance(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool address_balances(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool address_deposit(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool address_utxos(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
static bool address_utxos_amount(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);

SAPI::EndpointGroup addressEndpoints = {
    "address",
    {
        {
            "balance/{address}", HTTPRequest::GET, UniValue::VNULL, address_balance,
            {
                // No body parameter
            }
        },
        {
            "balances", HTTPRequest::POST, UniValue::VARR, address_balances,
            {
                // No body parameter
            }
        },
        {
            "deposit", HTTPRequest::POST, UniValue::VOBJ, address_deposit,
            {
                SAPI::BodyParameter(SAPI::Keys::address,        new SAPI::Validation::SmartCashAddress()),
                SAPI::BodyParameter(SAPI::Keys::timestampFrom,  new SAPI::Validation::UInt(), true),
                SAPI::BodyParameter(SAPI::Keys::timestampTo,    new SAPI::Validation::UInt(), true),
                SAPI::BodyParameter(SAPI::Keys::pageNumber,     new SAPI::Validation::IntRange(1,INT_MAX)),
                SAPI::BodyParameter(SAPI::Keys::pageSize,       new SAPI::Validation::IntRange(1,1000)),
                SAPI::BodyParameter(SAPI::Keys::ascending,      new SAPI::Validation::Bool(), true),
            }
        },
        {
            "unspent", HTTPRequest::POST, UniValue::VOBJ, address_utxos,
            {
                SAPI::BodyParameter(SAPI::Keys::address,        new SAPI::Validation::SmartCashAddress()),
                SAPI::BodyParameter(SAPI::Keys::pageNumber,     new SAPI::Validation::IntRange(1,INT_MAX)),
                SAPI::BodyParameter(SAPI::Keys::pageSize,       new SAPI::Validation::IntRange(1,1000))
            }
        },
        {
            "unspent/amount", HTTPRequest::POST, UniValue::VOBJ, address_utxos_amount,
            {
                SAPI::BodyParameter(SAPI::Keys::address,  new SAPI::Validation::SmartCashAddress()),
                SAPI::BodyParameter(SAPI::Keys::amount,   new SAPI::Validation::DoubleRange(1.0 / COIN,(double)MAX_MONEY / COIN)),
                SAPI::BodyParameter(SAPI::Keys::solution, new SAPI::Validation::IntRange(0,2), true),
                SAPI::BodyParameter(SAPI::Keys::instantpay, new SAPI::Validation::Bool(), true)
            }
        }
    }
};

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
        return Error(req, HTTPStatus::BAD_REQUEST, errors);
    }

    if( !vecBalances.size() ){
        return SAPI::Error(req, HTTPStatus::INTERNAL_SERVER_ERROR, "Balance check failed unexpected.");
    }


    return true;
}

static bool GetUTXOCount(HTTPRequest* req, const CBitcoinAddress& address, int &count, CAddressUnspentKey &lastIndex){

    uint160 hashBytes;
    int type = 0;

    if (!address.GetIndexKey(hashBytes, type)) {
        return Error(req, SAPI::InvalidSmartCashAddress, "Invalid address");
    }

    if (!GetAddressUnspentCount(hashBytes, type, count, lastIndex)) {
        return Error(req, SAPI::AddressNotFound, "No information available for address");
    }

    return true;
}

static bool GetUTXOs(HTTPRequest* req, const CBitcoinAddress& address, std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >& utxos,
                     const CAddressUnspentKey &start = CAddressUnspentKey(),
                     int offset = -1, int limit = -1, bool reverse = false){

    uint160 hashBytes;
    int type = 0;

    if (!address.GetIndexKey(hashBytes, type)) {
        return Error(req, SAPI::InvalidSmartCashAddress, "Invalid address");
    }

    if (!GetAddressUnspent(hashBytes, type, utxos, start, offset, limit, reverse)) {
        return Error(req, SAPI::AddressNotFound, "No information available for address");
    }

    return true;
}

inline CAmount CalculateFee( int nInputs )
{
    CAmount feeCalc = (((nInputs * 148) + (2 * 34) + 10 + 9) / 1024.0) * 100000;
    feeCalc = std::floor((feeCalc / 100000.0) + 0.5) * 100000;
    return std::max(feeCalc, static_cast<CAmount>(100000));
}

static bool address_balance(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{

    if ( !mapPathParams.count("address") )
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "No SmartCash address specified. Use /address/balance/<smartcash_address>");

    std::string addrStr = mapPathParams.at("address");
    std::vector<CAddressBalance> vecResult;

    if( !GetAddressesBalances(req, {addrStr},vecResult) )
        return false;

    CAddressBalance result = vecResult.front();

    UniValue response(UniValue::VOBJ);
    response.pushKV("address", result.address);
    response.pushKV("received", CAmountToDouble(result.received));
    response.pushKV("sent", CAmountToDouble(result.received - result.balance));
    response.pushKV("balance", CAmountToDouble(result.balance));

    SAPI::WriteReply(req, response);

    return true;
}


static bool address_balances(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{

    if( !bodyParameter.isArray() || bodyParameter.empty() )
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "Addresses are expedted to be a JSON array: [ \"address\", ... ]");

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
        entry.pushKV(SAPI::Keys::address, result.address);
        entry.pushKV("received", CAmountToDouble(result.received));
        entry.pushKV("sent", CAmountToDouble(result.received - result.balance));
        entry.pushKV("balance", CAmountToDouble(result.balance));
        response.push_back(entry);
    }

    SAPI::WriteReply(req, response);

    return true;
}

static bool address_deposit(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    int64_t nTime0, nTime1, nTime2, nTime3, nTime4, nTime5;

    nTime0 = GetTimeMicros();

    std::string addrStr = bodyParameter[SAPI::Keys::address].get_str();
    int64_t start = bodyParameter.exists(SAPI::Keys::timestampFrom) ? bodyParameter[SAPI::Keys::timestampFrom].get_int64() : 0;
    int64_t end = bodyParameter.exists(SAPI::Keys::timestampTo) ? bodyParameter[SAPI::Keys::timestampTo].get_int64() : INT_MAX;
    int64_t nPageNumber = bodyParameter[SAPI::Keys::pageNumber].get_int64();
    int64_t nPageSize = bodyParameter[SAPI::Keys::pageSize].get_int64();
    bool fAsc = bodyParameter.exists(SAPI::Keys::ascending) ? bodyParameter[SAPI::Keys::ascending].get_bool() : false;

    if ( end <= start)
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "\"" + SAPI::Keys::timestampFrom + "\" is expected to be greater than \"" + SAPI::Keys::timestampTo + "\"");

    CBitcoinAddress address(addrStr);
    uint160 hashBytes;
    int type = 0;
    int nDeposits = 0;
    int nFirstTimestamp;
    int nLastTimestamp;

    if (!address.GetIndexKey(hashBytes, type))
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST,"Invalid address: " + addrStr);

    std::vector<std::pair<CDepositIndexKey, CDepositValue> > depositIndex;

    nTime1 = GetTimeMicros();

    if (!GetDepositIndexCount(hashBytes, type, nDeposits, nFirstTimestamp, nLastTimestamp, start, end) )
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "No information available for the provided timerange.");

    if (!nDeposits)
        return SAPI::Error(req, SAPI::NoDepositAvailble, "No deposits available for the given timerange.");

    int nPages = nDeposits / nPageSize;
    if( nDeposits % nPageSize ) nPages++;

    if (nPageNumber > nPages)
        return SAPI::Error(req, SAPI::PageOutOfRange, strprintf("Page number out of range: 1 - %d", nPages));

    int nIndexOffset = static_cast<int>(( nPageNumber - 1 ) * nPageSize);
    int nLimit = static_cast<int>((nDeposits % nPageSize) && nPageNumber == nPages ? (nDeposits % nPageSize) : nPageSize);

    nTime2 = GetTimeMicros();

    if (!GetDepositIndex(hashBytes, type, depositIndex, fAsc ? nFirstTimestamp : nLastTimestamp, nIndexOffset , nLimit, !fAsc))
        return SAPI::Error(req, HTTPStatus::BAD_REQUEST, "No information available for " + addrStr);

    nTime3 = GetTimeMicros();

    UniValue result(UniValue::VOBJ);

    UniValue arrDeposit(UniValue::VARR);

    for (std::vector<std::pair<CDepositIndexKey, CDepositValue> >::const_iterator it=depositIndex.begin();
         it!=depositIndex.end();
         it++) {

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("txhash", it->first.txhash.GetHex());
        obj.pushKV("blockHeight", it->second.blockHeight);
        obj.pushKV("timestamp", int64_t(it->first.timestamp));
        obj.pushKV("amount", CAmountToDouble(it->second.satoshis));

        arrDeposit.push_back(obj);
    }

    UniValue obj(UniValue::VOBJ);

    obj.pushKV("count", nDeposits);
    obj.pushKV("pages", nPages);
    obj.pushKV("page", nPageNumber);
    obj.pushKV("deposits",arrDeposit);

    nTime4 = GetTimeMicros();

    SAPI::WriteReply(req, obj);

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

static bool address_utxos(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    int64_t nTime0, nTime1, nTime2, nTime3, nTime4;

    nTime0 = GetTimeMicros();

    std::string addrStr = bodyParameter[SAPI::Keys::address].get_str();
    int64_t nPageNumber = bodyParameter[SAPI::Keys::pageNumber].get_int64();
    int64_t nPageSize = bodyParameter[SAPI::Keys::pageSize].get_int64();
    bool fAsc = bodyParameter.exists(SAPI::Keys::ascending) ? bodyParameter[SAPI::Keys::ascending].get_bool() : false;

    CBitcoinAddress address(addrStr);

    CAddressUnspentKey lastIndex;
    int nUtxoCount = 0;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    if( !GetUTXOCount(req, address, nUtxoCount, lastIndex ) ){
        return false;
    }

    if (!nUtxoCount)
        return SAPI::Error(req, SAPI::NoUtxosAvailble, "No unspent outputs available.");

    nTime1 = GetTimeMicros();

    int nPages = nUtxoCount / nPageSize;
    if( nUtxoCount % nPageSize ) nPages++;

    if (nPageNumber > nPages)
        return SAPI::Error(req, SAPI::PageOutOfRange, strprintf("Page number out of range: 1 - %d", nPages));

    int nIndexOffset = static_cast<int>(( nPageNumber - 1 ) * nPageSize);
    int nLimit = static_cast<int>( (nUtxoCount % nPageSize) && nPageNumber == nPages ? (nUtxoCount % nPageSize) : nPageSize);

    if (!GetUTXOs(req, address, unspentOutputs, fAsc ? CAddressUnspentKey() : lastIndex, nIndexOffset , nLimit, !fAsc))
        return false;

    nTime2 = GetTimeMicros();

    UniValue arrUtxos(UniValue::VARR);

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++) {
        UniValue output(UniValue::VOBJ);
        std::string address;

        CSpentIndexValue spentInfo;
        CSpentIndexKey spentKey(it->first.txhash, static_cast<unsigned int>(it->first.index));

        // Mark inputs currently used for tx in the mempool
        bool fInMempool = mempool.getSpentIndex(spentKey, spentInfo);

        output.pushKV("txid", it->first.txhash.GetHex());
        output.pushKV("index", static_cast<int>(it->first.index));
        output.pushKV(SAPI::Keys::address, address);
        output.pushKV("script", HexStr(it->second.script.begin(), it->second.script.end()));
        output.pushKV("value", CAmountToDouble(it->second.satoshis));
        output.pushKV("height", it->first.nBlockHeight);
        output.pushKV("inMempool", fInMempool);

        arrUtxos.push_back(output);
    }

    nTime3 = GetTimeMicros();

    UniValue obj(UniValue::VOBJ);

    obj.pushKV("count", nUtxoCount);
    obj.pushKV("pages", nPages);
    obj.pushKV("page", nPageNumber);
    obj.pushKV("blockHeight", chainActive.Height());
    obj.pushKV("utxos",arrUtxos);

    SAPI::WriteReply(req, obj);

    nTime4 = GetTimeMicros();

    LogPrint("sapi-benchmark", "\naddress_utxos\n");
    LogPrint("sapi-benchmark", " Query utxos count: %.2fms\n", (nTime1 - nTime0) * 0.001);
    LogPrint("sapi-benchmark", " Query utxos: %.2fms\n", (nTime2 - nTime1) * 0.001);
    LogPrint("sapi-benchmark", " Process utxos: %.2fms\n", (nTime3 - nTime2) * 0.001);
    LogPrint("sapi-benchmark", " Write reply: %.2fms\n", (nTime4 - nTime3) * 0.001);
    LogPrint("sapi-benchmark", " Total: %.2fms\n\n", (nTime4 - nTime0) * 0.001);

    return true;
}

static bool address_utxos_amount(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter)
{
    int64_t nTime0, nTime1, nTime2, nTime3, nTime4;

    // matching algorithm parameters
    const int nUtxosSlice = 1000;
    const int nMatchTimeoutMicros = 5 * 1000000;

    nTime0 = GetTimeMicros();

    std::string addrStr = bodyParameter[SAPI::Keys::address].get_str();
    CAmount expectedAmount = bodyParameter[SAPI::Keys::amount].get_real() * COIN;
    int64_t nSolution = bodyParameter.exists(SAPI::Keys::solution) ? bodyParameter[SAPI::Keys::solution].get_int64() : 0;
    bool fInstantPay = bodyParameter.exists(SAPI::Keys::instantpay) ? bodyParameter[SAPI::Keys::instantpay].get_bool() : false;

    CBitcoinAddress address(addrStr);
    CAddressUnspentKey lastIndex;
    int nUtxoCount = 0;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    if( !GetUTXOCount(req, address, nUtxoCount, lastIndex ) ){
        return false;
    }

    if (!nUtxoCount)
        return SAPI::Error(req, SAPI::NoUtxosAvailble, "No unspent outputs available.");

    nTime1 = GetTimeMicros();

    int nPages = nUtxoCount / nUtxosSlice;
    if( nUtxoCount % nUtxosSlice ) nPages++;
    int nPageStart = GetRand(nPages);
    int nPageCurrent = nPageStart;

    int64_t nHeight = chainActive.Height();

    CUnspentSolution bestSolution;
    std::pair<CAddressUnspentKey, CAddressUnspentValue> nearestMatch;

    nearestMatch.first.SetNull();

    do{

        int nIndexOffset = static_cast<int>( (nPageCurrent % nPages) * nUtxosSlice);
        int nLimit = static_cast<int>( (nUtxoCount % nUtxosSlice) &&
                                       (nPageCurrent % nPages) == nPages - 1 ? (nUtxoCount % nUtxosSlice) :
                                                                    nUtxosSlice);

        if( nSolution && GetTimeMicros() - nTime0 > nMatchTimeoutMicros )
            break;

        if( !GetUTXOs(req, address, unspentOutputs, CAddressUnspentKey(), nIndexOffset , nLimit) )
            return false;

        CAmount fee = CalculateFee(0);
        CAmount amountSum = 0;

        UniValue utxos(UniValue::VARR);

        switch(nSolution){
        case 0:{ // Pick random utxos until the amount is reached.
            auto rng = std::default_random_engine {};
            std::shuffle(unspentOutputs.begin(), unspentOutputs.end(), rng);
        }break;
        case 1:{ // Search a solution with fewest change

            // If we are in the second iteration use the nearest match from the
            // round before too..
            if( !nearestMatch.first.IsNull() ){
                unspentOutputs.push_back(nearestMatch);
                nearestMatch.first.SetNull();
            }

            std::sort(unspentOutputs.begin(), unspentOutputs.end(), amountSortLTH);

            auto it = unspentOutputs.rbegin();

            while( it++ != unspentOutputs.rend() ) {
                if( it->second.satoshis <= expectedAmount ){
                    nearestMatch = *it;
                    break;
                }
            }

            if( it != unspentOutputs.rend() ){

                UniValue obj(UniValue::VOBJ);

                obj.pushKV("txid", it->first.txhash.GetHex());
                obj.pushKV("index", static_cast<int>(it->first.index));
                obj.pushKV("confirmations", nHeight - it->first.nBlockHeight + 1);
                obj.pushKV("amount", UniValue(CAmountToDouble(it->second.satoshis),8));

                utxos.push_back(obj);

                fee = CalculateFee(static_cast<int>(utxos.size()));

                unspentOutputs.erase( it.base() - 1 );
            }

        }break;
        case 2: // Search a solution with fewest utxo's
            std::sort(unspentOutputs.begin(), unspentOutputs.end(), amountSortHTL);
            break;
        default:
            // Won't happen.
            break;
        }

        for (auto it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++) {

            if( nSolution && GetTimeMicros() - nTime0 > nMatchTimeoutMicros )
                break;

            CSpentIndexValue spentInfo;
            CSpentIndexKey spentKey(it->first.txhash, static_cast<unsigned int>(it->first.index));

            // Ignore inputs currently used for tx in the mempool
            // Ignore inputs that are not valid for instantpay if instantpay is requested
            if (!mempool.getSpentIndex(spentKey, spentInfo) &&
               ( !fInstantPay || ( fInstantPay && (nHeight - it->first.nBlockHeight + 1) >= INSTANTSEND_CONFIRMATIONS_REQUIRED ) ) ){

                amountSum += it->second.satoshis;

                UniValue obj(UniValue::VOBJ);

                obj.pushKV("txid", it->first.txhash.GetHex());
                obj.pushKV("index", static_cast<int>(it->first.index));
                obj.pushKV("confirmations", nHeight - it->first.nBlockHeight + 1);
                obj.pushKV("amount", UniValue(CAmountToDouble(it->second.satoshis),8));

                utxos.push_back(obj);

                fee = CalculateFee(static_cast<int>(utxos.size()));
            }

            if( amountSum > expectedAmount + fee ){

                CAmount change = amountSum - expectedAmount - fee;

                if( bestSolution.IsNull() ||
                    ( nSolution == 1 && change < bestSolution.change ) || // Looking for fewest change
                    ( nSolution == 2 && utxos.size() < bestSolution.arrUTXOs.size() ) ){ // Looking for fewest inputs

                    bestSolution = CUnspentSolution(amountSum,fee,change,utxos);
                }

                break;
            }
        }

        if( ( !bestSolution.IsNull() && !nSolution ) ||
            ( nSolution && GetTimeMicros() - nTime0 > nMatchTimeoutMicros ) )
            break;

    }while( (++nPageCurrent % nPages) != nPageStart);

    nTime2 = GetTimeMicros();

    // If we iterated over all utxos and we did not find a solution.
    if( (++nPageCurrent % nPages) == nPageStart && bestSolution.IsNull() )
        return SAPI::Error(req, SAPI::BalanceTooLow, "Requested amount exceeds balance");

    // We found no solution, but there might be one..
    if( bestSolution.IsNull() )
        return SAPI::Error(req, SAPI::TimedOut, "No solution found");

    nTime3 = GetTimeMicros();

    UniValue result(UniValue::VOBJ);

    CScript script = GetScriptForDestination(address.Get());

    result.pushKV("blockHeight", nHeight);
    result.pushKV("scriptPubKey", HexStr(script.begin(), script.end()));
    result.pushKV("address", addrStr);
    result.pushKV("amount", UniValue(CAmountToDouble(bestSolution.amount),8));
    result.pushKV("fee", UniValue(CAmountToDouble(bestSolution.fee),8));
    result.pushKV("change", UniValue(CAmountToDouble(bestSolution.change),8));
    result.pushKV("utxos", bestSolution.arrUTXOs);

    SAPI::WriteReply(req, result);

    nTime4 = GetTimeMicros();

    LogPrint("sapi-benchmark", "\naddress_utxos_amount\n");
    LogPrint("sapi-benchmark", " Query utxos: %.2fms\n", (nTime2 - nTime1) * 0.001);
    LogPrint("sapi-benchmark", " Evaluate inputs: %.2fms\n", (nTime2 - nTime3) * 0.001);
    LogPrint("sapi-benchmark", " Write reply: %.2fms\n", (nTime3 - nTime4) * 0.001);
    LogPrint("sapi-benchmark", " Total: %.2fms\n\n", (nTime3 - nTime0) * 0.001);

    return true;
}
