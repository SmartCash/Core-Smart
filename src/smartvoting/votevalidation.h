// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTVOTING_VOTEVALIDATION_H
#define SMARTVOTING_VOTEVALIDATION_H

#include <list>
#include <map>

#include "smarthive/hive.h"
#include "voting.h"
#include "serialize.h"
#include "streams.h"
#include "uint256.h"

// Update all vote's voting power every nValidationInterval blocks
static const int nValidationInterval = 1;
static const int nValidationConfirmations = 2;

struct CVotingPower{
    int nBlockHeight;
    CAmount nPower;
    CSmartAddress address;

    CVotingPower(){ SetNull(); }
    CVotingPower(const CSmartAddress &address) : nBlockHeight(0), nPower(0), address(address) {}

    bool IsValid() const { return nBlockHeight > 0; }
    void SetNull() {
        nBlockHeight = -1;
        nPower = -1;
        address = CSmartAddress();
    }
};

void ThreadSmartVoting();
void AddActiveVoteKey(const CVoteKey &voteKey);
void GetVotingPower(const CVoteKey &voteKey, CVotingPower &votingPower);
CAmount GetVotingPower(const CVoteKey &voteKey);

#endif
