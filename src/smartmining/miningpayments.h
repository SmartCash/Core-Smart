// Copyright (c) 2018 dustinface - SmartCash Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MININNGPAYMENTS_H
#define MININNGPAYMENTS_H

#include "amount.h"
#include "chain.h"
#include "coins.h"
#include "base58.h"
#include "validation.h"

namespace SmartMining{

bool Validate(const CBlock& block, CBlockIndex *pindex, CValidationState& state, CAmount nFees);
void FillPayment(CMutableTransaction& txNew, int nHeight, CBlockIndex * pindexPrev, CAmount blockReward);

}

#endif // MININNGPAYMENTS_H
