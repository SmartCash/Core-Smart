// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VOTING_H
#define VOTING_H

#include "net.h"
#include "key.h"
#include "primitives/transaction.h"

#include <boost/lexical_cast.hpp>

// INTENTION OF SMARTNODES REGARDING ITEM
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
    VOTE_SIGNAL_VALID      = 2, //   -- this proposal checks out in sentinel engine
    VOTE_SIGNAL_DELETE     = 3, //   -- this proposal should be deleted from memory entirely
};

static const int MAX_SUPPORTED_VOTE_SIGNAL = VOTE_SIGNAL_DELETE;

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
    CPubKey votingKey;
    uint256 nProposalHash;
    int nVoteOutcome; // see VOTE_OUTCOMES above
    int64_t nTime;
    std::vector<unsigned char> vchSig;

    /** Memory only. */
    const uint256 hash;
    void UpdateHash() const;

public:
    CProposalVote();
    CProposalVote(const CPubKey& votingKeyIn, const uint256& nParentHashIn, vote_signal_enum_t eVoteSignalIn, vote_outcome_enum_t eVoteOutcomeIn);

    bool IsValid() const { return fValid; }

    bool IsSynced() const { return fSynced; }

    int64_t GetTimestamp() const { return nTime; }

    vote_signal_enum_t GetSignal() const  { return vote_signal_enum_t(nVoteSignal); }

    vote_outcome_enum_t GetOutcome() const  { return vote_outcome_enum_t(nVoteOutcome); }

    const uint256& GetProposalHash() const { return nProposalHash; }

    void SetTime(int64_t nTimeIn) { nTime = nTimeIn; UpdateHash(); }

    void SetSignature(const std::vector<unsigned char>& vchSigIn) { vchSig = vchSigIn; }

    bool Sign(const CKey& keyVotingKey);
    bool CheckSignature() const;
    bool IsValid(bool fSignatureCheck) const;
    void Relay(CConnman& connman) const;

    std::string GetVoteString() const {
        return CProposalVoting::ConvertOutcomeToString(GetOutcome());
    }

    const CPubKey& GetVotingKey() const { return votingKey; }

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
        READWRITE(votingKey);
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

#endif // VOTING_H
