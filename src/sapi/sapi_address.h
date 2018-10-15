// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_SAPI_ADDRESS_H
#define SMARTCASH_SAPI_ADDRESS_H

#include "sapi/sapi.h"

struct CAddressUnspentKey;
struct CAddressUnspentValue;

extern SAPI::EndpointGroup addressEndpoints;

struct CUnspentSolution{
    CAmount amount;
    CAmount fee;
    CAmount change;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> vecUtxos;
    CUnspentSolution() { SetNull();}

    void SetNull(){
        amount = 0;
        fee = 0;
        change = 0;
        vecUtxos.clear();
    }
    bool IsNull(){ return !vecUtxos.size(); }

    void AddUtxo(const std::pair<CAddressUnspentKey, CAddressUnspentValue> &utxo);
};

#endif // SMARTCASH_SAPI_ADDRESS_H
