// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartvoting/voting.h"
#include "smartvoting/proposal.h"
#include "smartnode/smartnodesync.h"
#include "spentindex.h"
#include "messagesigner.h"
#include "util.h"

bool IsRegisteredForVoting(const CVoteKey &voteKey);

std::string CProposalVoting::ConvertOutcomeToString(vote_outcome_enum_t nOutcome)
{
    switch(nOutcome)
    {
        case VOTE_OUTCOME_NONE:
            return "NONE"; break;
        case VOTE_OUTCOME_YES:
            return "YES"; break;
        case VOTE_OUTCOME_NO:
            return "NO"; break;
        case VOTE_OUTCOME_ABSTAIN:
            return "ABSTAIN"; break;
    }
    return "error";
}

std::string CProposalVoting::ConvertSignalToString(vote_signal_enum_t nSignal)
{
    std::string strReturn = "NONE";
    switch(nSignal)
    {
        case VOTE_SIGNAL_NONE:
            strReturn = "NONE";
            break;
        case VOTE_SIGNAL_FUNDING:
            strReturn = "FUNDING";
            break;
        case VOTE_SIGNAL_VALID:
            strReturn = "VALID";
            break;
    }

    return strReturn;
}


vote_outcome_enum_t CProposalVoting::ConvertVoteOutcome(const std::string& strVoteOutcome)
{
    vote_outcome_enum_t eVote = VOTE_OUTCOME_NONE;
    if(strVoteOutcome == "yes") {
        eVote = VOTE_OUTCOME_YES;
    }
    else if(strVoteOutcome == "no") {
        eVote = VOTE_OUTCOME_NO;
    }
    else if(strVoteOutcome == "abstain") {
        eVote = VOTE_OUTCOME_ABSTAIN;
    }
    return eVote;
}

vote_signal_enum_t CProposalVoting::ConvertVoteSignal(const std::string& strVoteSignal)
{
    static const std::map <std::string, vote_signal_enum_t> mapStrVoteSignals = {
        {"funding",  VOTE_SIGNAL_FUNDING},
        {"valid",    VOTE_SIGNAL_VALID}
    };

    const auto& it = mapStrVoteSignals.find(strVoteSignal);
    if (it == mapStrVoteSignals.end()) {
        LogPrintf("CProposalVoting::%s -- ERROR: Unknown signal %s\n", __func__, strVoteSignal);
        return VOTE_SIGNAL_NONE;
    }
    return it->second;
}

CProposalVote::CProposalVote()
    : fValid(true),
      fSynced(false),
      nVoteSignal(int(VOTE_SIGNAL_NONE)),
      voteKey(),
      nProposalHash(),
      nVoteOutcome(int(VOTE_OUTCOME_NONE)),
      nTime(0),
      vchSig()
{}

CProposalVote::CProposalVote(const CVoteKey& voteKeyIn, const uint256& nProposalHashIn, vote_signal_enum_t eVoteSignalIn, vote_outcome_enum_t eVoteOutcomeIn)
    : fValid(true),
      fSynced(false),
      nVoteSignal(eVoteSignalIn),
      voteKey(voteKeyIn),
      nProposalHash(nProposalHashIn),
      nVoteOutcome(eVoteOutcomeIn),
      nTime(GetAdjustedTime()),
      vchSig()
{
    UpdateHash();
}

std::string CProposalVote::ToString() const
{
    std::ostringstream ostr;
    ostr << voteKey.ToString() << ":"
         << nTime << ":"
         << CProposalVoting::ConvertOutcomeToString(GetOutcome()) << ":"
         << CProposalVoting::ConvertSignalToString(GetSignal());
    return ostr.str();
}

void CProposalVote::Relay(CConnman &connman) const
{
    // Do not relay until fully synced
    if(!smartnodeSync.IsSynced()) {
        LogPrint("proposal", "CProposalVote::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_VOTING_PROPOSAL_VOTE, GetHash());
    connman.RelayInv(inv, MIN_SMARTVOTING_PEER_PROTO_VERSION);
}


void CProposalVote::UpdateHash() const
{
    // Note: doesn't match serialization

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << voteKey;
    ss << nProposalHash;
    ss << nVoteSignal;
    ss << nVoteOutcome;
    ss << nTime;
    *const_cast<uint256*>(&hash) = ss.GetHash();
}

uint256 CProposalVote::GetHash() const
{
    return hash;
}

uint256 CProposalVote::GetSignatureHash() const
{
    return SerializeHash(*this);
}

bool CProposalVote::Sign(const CVoteKeySecret& voteKeySecret)
{
    std::string strError;

    uint256 hash = GetSignatureHash();

    if(!CHashSigner::SignHash(hash, voteKeySecret.GetKey(), vchSig)) {
        LogPrintf("CProposalVote::Sign -- SignHash() failed\n");
        return false;
    }

    CKeyID keyId;
    if ( !voteKey.GetKeyID(keyId) || !CHashSigner::VerifyHash(hash, keyId, vchSig, strError)) {
        LogPrintf("CProposalVote::Sign -- VerifyHash() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CProposalVote::CheckSignature() const
{
    std::string strError;

    uint256 hash = GetSignatureHash();
    CKeyID keyId;

    if ( !voteKey.GetKeyID(keyId) || !CHashSigner::VerifyHash(hash, keyId, vchSig, strError)) {
        LogPrint("proposal", "CProposalVote::IsValid -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CProposalVote::IsValid(bool fSignatureCheck, bool fRegistrationCheck, std::string &strError) const
{
    if(nTime > GetAdjustedTime() + (60*60)) {
        strError = strprintf("vote is too far ahead of current time - %s - nTime %lli - Max Time %lli", GetHash().ToString(), nTime, GetAdjustedTime() + (60*60));
        LogPrint("proposal", (strError + "\n").c_str());
        return false;
    }

    // support up to MAX_SUPPORTED_VOTE_SIGNAL, can be extended
    if(nVoteSignal > MAX_SUPPORTED_VOTE_SIGNAL) {
        strError = strprintf("CProposalVote::IsValid -- Client attempted to vote on invalid signal(%d) - %s", nVoteSignal, GetHash().ToString());
        LogPrint("proposal", (strError + "\n").c_str());
        return false;
    }

    // 0=none, 1=yes, 2=no, 3=abstain. Beyond that reject votes
    if(nVoteOutcome > 3) {
        strError = strprintf("CProposalVote::IsValid -- Client attempted to vote on invalid outcome(%d) - %s", nVoteSignal, GetHash().ToString());
        LogPrint("proposal", (strError + "\n").c_str());
        return false;
    }

    if( fRegistrationCheck && !IsRegisteredForVoting(voteKey) ){
        strError = strprintf("CProposalVote::IsValid -- No registered voteKey %s", voteKey.ToString());
        LogPrint("proposal", (strError + "\n").c_str());
        return false;
    }

    if(!fSignatureCheck) return true;

    if(!CheckSignature() ){
        strError = "Signature check failed";
        LogPrint("proposal", (strError + "\n").c_str());
    }

    return true;
}

bool operator==(const CProposalVote& vote1, const CProposalVote& vote2)
{
    bool fResult = ((vote1.voteKey == vote2.voteKey) &&
                    (vote1.nProposalHash == vote2.nProposalHash) &&
                    (vote1.nVoteOutcome == vote2.nVoteOutcome) &&
                    (vote1.nVoteSignal == vote2.nVoteSignal) &&
                    (vote1.nTime == vote2.nTime));
    return fResult;
}

bool operator<(const CProposalVote& vote1, const CProposalVote& vote2)
{
    bool fResult = (vote1.voteKey < vote2.voteKey);
    if(!fResult) {
        return false;
    }
    fResult = (vote1.voteKey == vote2.voteKey);

    fResult = fResult && (vote1.nProposalHash < vote2.nProposalHash);
    if(!fResult) {
        return false;
    }
    fResult = fResult && (vote1.nProposalHash == vote2.nProposalHash);

    fResult = fResult && (vote1.nVoteOutcome < vote2.nVoteOutcome);
    if(!fResult) {
        return false;
    }
    fResult = fResult && (vote1.nVoteOutcome == vote2.nVoteOutcome);

    fResult = fResult && (vote1.nVoteSignal == vote2.nVoteSignal);
    if(!fResult) {
        return false;
    }
    fResult = fResult && (vote1.nVoteSignal == vote2.nVoteSignal);

    fResult = fResult && (vote1.nTime < vote2.nTime);

    return fResult;
}
