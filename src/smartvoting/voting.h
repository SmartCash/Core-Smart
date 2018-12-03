// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VOTING_H
#define VOTING_H

#include "base58.h"
#include "net.h"
#include "key.h"
#include "primitives/transaction.h"
#include "univalue.h"

#include <boost/lexical_cast.hpp>

// INTENTION OF VOTE REGARDING ITEM
enum vote_outcome_enum_t  {
    VOTE_OUTCOME_NONE      = 0,
    VOTE_OUTCOME_YES       = 1,
    VOTE_OUTCOME_NO        = 2,
    VOTE_OUTCOME_ABSTAIN   = 3
};


// SIGNAL VARIOUS THINGS TO HAPPEN:
enum vote_signal_enum_t  {
    VOTE_SIGNAL_NONE       = 0,
    VOTE_SIGNAL_FUNDING    = 1, //   -- fund this proposal for it's stated amount
    VOTE_SIGNAL_VALID     = 2, //   -- this proposal should be deleted from memory entirely
};

static const int MAX_SUPPORTED_VOTE_SIGNAL = VOTE_SIGNAL_VALID;

/**
* Governance Voting
*
*   Static class for accessing governance data
*/

class CProposalVoting
{
public:
    static vote_outcome_enum_t ConvertVoteOutcome(const std::string& strVoteOutcome);
    static vote_signal_enum_t ConvertVoteSignal(const std::string& strVoteSignal);
    static std::string ConvertOutcomeToString(vote_outcome_enum_t nOutcome);
    static std::string ConvertSignalToString(vote_signal_enum_t nSignal);
};

//
// CProposalVote - Allow a voting key to vote and broadcast throughout the network
//

class CProposalVote
{
    friend bool operator==(const CProposalVote& vote1, const CProposalVote& vote2);

    friend bool operator<(const CProposalVote& vote1, const CProposalVote& vote2);

private:
    bool fValid; //if the vote is currently valid / counted
    bool fSynced; //if we've sent this to our peers
    int nVoteSignal; // see VOTE_ACTIONS above
    CVoteKey voteKey;
    uint256 nProposalHash;
    int nVoteOutcome; // see VOTE_OUTCOMES above
    int64_t nTime;
    std::vector<unsigned char> vchSig;

    /** Memory only. */
    const uint256 hash;
    void UpdateHash() const;

public:
    CProposalVote();
    CProposalVote(const CVoteKey& voteKeyIn, const uint256& nParentHashIn, vote_signal_enum_t eVoteSignalIn, vote_outcome_enum_t eVoteOutcomeIn);

    bool IsValid() const { return fValid; }

    bool IsSynced() const { return fSynced; }

    int64_t GetTimestamp() const { return nTime; }

    vote_signal_enum_t GetSignal() const  { return vote_signal_enum_t(nVoteSignal); }

    vote_outcome_enum_t GetOutcome() const  { return vote_outcome_enum_t(nVoteOutcome); }

    const uint256& GetProposalHash() const { return nProposalHash; }

    void SetTime(int64_t nTimeIn) { nTime = nTimeIn; UpdateHash(); }

    void SetSignature(const std::vector<unsigned char>& vchSigIn) { vchSig = vchSigIn; }

    bool Sign(const CVoteKeySecret& voteKeySecret);
    bool CheckSignature() const;
    bool IsValid(bool fSignatureCheck, bool fRegistrationCheck, std::string &strError) const;
    void Relay(CConnman& connman) const;

    std::string GetVoteString() const {
        return CProposalVoting::ConvertOutcomeToString(GetOutcome());
    }

    const CVoteKey& GetVoteKey() const { return voteKey; }

    /**
    *   GetHash()
    *
    *   GET UNIQUE HASH WITH DETERMINISTIC VALUE OF THIS SPECIFIC VOTE
    */

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;

    std::string ToString() const;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(voteKey);
        READWRITE(nProposalHash);
        READWRITE(nVoteOutcome);
        READWRITE(nVoteSignal);
        READWRITE(nTime);
        if (!(nType & SER_GETHASH)) {
            READWRITE(vchSig);
        }
        if (ser_action.ForRead())
            UpdateHash();
    }

};

struct CVoteOutcomes{

    int64_t nYesPower;
    int64_t nNoPower;
    int64_t nAbstainPower;

    CVoteOutcomes() : nYesPower(0), nNoPower(0), nAbstainPower(0) {}
    CVoteOutcomes(CAmount nYes, CAmount nNo, CAmount nAbstain):
                nYesPower(nYes), nNoPower(nNo), nAbstainPower(nAbstain) {

    }

    int64_t GetTotalPower() { return nYesPower+nNoPower+nAbstainPower; }
};


struct CVoteResult : CVoteOutcomes{

    double percentYes;
    double percentNo;
    double percentAbstain;

    CVoteResult(CAmount nYes, CAmount nNo, CAmount nAbstain):
                CVoteOutcomes(nYes, nNo, nAbstain),
                percentYes(0.0), percentNo(0.0), percentAbstain(0.0){

        if( GetTotalPower() ){
            percentYes = ( static_cast<double>(nYesPower) / GetTotalPower() ) * 100;
            percentNo = ( static_cast<double>(nNoPower) / GetTotalPower() ) * 100;
            percentAbstain = ( static_cast<double>(nAbstainPower) / GetTotalPower() ) * 100;
        }
    }
};

#endif // VOTING_H
