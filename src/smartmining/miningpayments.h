// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MININNGPAYMENTS_H
#define MININNGPAYMENTS_H

#include "amount.h"
#include "chain.h"
#include "coins.h"
#include "base58.h"
#include "validation.h"

const uint8_t OP_DATA_MINING_FLAG = 0x01;
const int nMiningSignaturePastTimeCutoff = 7200; // 2 hours
const int nMiningSignatureScriptLength = 70;

namespace SmartMining{

bool Validate(const CBlock& block, CBlockIndex *pindex, CValidationState& state, CAmount nFees, bool fJustCheck = false);
void FillPayment(CMutableTransaction& txNew, int nHeight, CBlockIndex * pindexPrev, CAmount blockReward);
bool IsSignatureRequired(const CBlock &block);

}

#endif // MININNGPAYMENTS_H
