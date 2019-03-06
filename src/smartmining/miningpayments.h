// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MININNGPAYMENTS_H
#define MININNGPAYMENTS_H

#include "amount.h"
#include "chain.h"
#include "coins.h"
#include "base58.h"
#include "validation.h"

struct CSmartAddress;

const int nMiningSignaturePastTimeCutoff = 3600; // 1 hour
const int nMiningSignatureMinScriptLength = 4;

extern CCriticalSection cs_miningkeys;

extern std::map<uint8_t, CSmartAddress> mapMiningKeysMainnet;
extern std::map<uint8_t, CSmartAddress> mapMiningKeysTestnet;

namespace SmartMining{

bool SetMiningKey(std::string &address);
bool Validate(const CBlock& block, CBlockIndex *pindex, CValidationState& state, CAmount nFees);
void FillPayment(CMutableTransaction& txNew, int nHeight, CBlockIndex * pindexPrev, CAmount blockReward, CTxOut &outSignature, const CSmartAddress &signingAddress);
bool IsSignatureRequired(const CBlockIndex *pindex);
bool IsSignatureRequired(const int nHeight);

}

#endif // MININNGPAYMENTS_H
