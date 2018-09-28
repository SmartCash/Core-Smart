// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_SAPI_ADDRESS_H
#define SMARTCASH_SAPI_ADDRESS_H

#include "sapi/sapi.h"

extern SAPI::EndpointGroup addressEndpoints;

struct CUnspentSolution{
    CAmount amount;
    CAmount fee;
    CAmount change;
    UniValue arrUTXOs;
    CUnspentSolution() { SetNull();}
    CUnspentSolution(CAmount amount, CAmount fee, CAmount change,
                     UniValue &arrUTXOs) : amount(amount),
                                          fee(fee),
                                          change(change),
                                          arrUTXOs(arrUTXOs){}

    void SetNull(){
        amount = 0.0;
        fee = 0.0;
        change = 0.0;
        arrUTXOs = UniValue(UniValue::VNULL);
    }
    bool IsNull(){ return arrUTXOs.isNull(); }
};

#endif // SMARTCASH_SAPI_ADDRESS_H
