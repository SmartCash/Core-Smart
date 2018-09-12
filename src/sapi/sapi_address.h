// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_SAPI_ADDRESS_H
#define SMARTCASH_SAPI_ADDRESS_H

#include <stdint.h>
#include <string>
#include <univalue.h>

class CSAPIDeposit{
    std::string txHash;
    int64_t timestamp;
    double amount;

public:
    CSAPIDeposit(std::string txHash, int64_t timestamp, double amount)
        : txHash(txHash),
          timestamp(timestamp),
          amount(amount){}

    int64_t GetTimestamp() const { return timestamp; }

    UniValue ToJson(){
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("txhash", txHash);
        obj.pushKV("timestamp", timestamp);
        obj.pushKV("amount", amount);
        return obj;
    }
};

#endif // SMARTCASH_SAPI_ADDRESS_H
