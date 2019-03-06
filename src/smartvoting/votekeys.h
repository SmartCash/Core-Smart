// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VOTEKEYS_H
#define VOTEKEYS_H

#include "amount.h"

static const CAmount VOTEKEY_REGISTER_FEE = 1 * COIN;
static const CAmount VOTEKEY_REGISTER_TX_FEE = 0.002 * COIN;

static const int VOTEKEY_REGISTRATION_O1_SCRIPT_SIZE = 0x5E;
static const int VOTEKEY_REGISTRATION_O1_DATA_SIZE = 0x5B;

static const int VOTEKEY_REGISTRATION_O2_SCRIPT_SIZE = 0xB7;
static const int VOTEKEY_REGISTRATION_O2_DATA_SIZE = 0xB4;

enum VoteKeyParseResult{
    Valid,
    TxResolveFailed,
    AddressResolveFailed,
    InvalidRegisterOption,
    InvalidVoteKey,
    InvalidVoteKeySignature,
    InvalidVoteAddress,
    InvalidVoteAddressSignature,
    IsNoRegistrationTx,
    VoteKeyAlreadyRegistered,
    VoteAddressAlreadyRegistered
};

#endif // VOTEKEYS_H
