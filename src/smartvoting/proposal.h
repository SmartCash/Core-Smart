// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PROPOSAL_H
#define PROPOSAL_H

#include "amount.h"
#include "base58.h"
#include "cachemultimap.h"
#include "chain.h"
#include "coins.h"
#include "net.h"
#include "smarthive/hive.h"
#include "smartvoting/exceptions.h"
#include "smartvoting/voting.h"
#include "smartvoting/votedb.h"

class CSmartVotingManager;
class CProposal;
class CProposalVote;

static const int SMARTVOTING_START_HEIGHT = 859100;

static const int MAX_SMARTVOTING_OBJECT_DATA_SIZE = 16 * 1024;
static const int MIN_SMARTVOTING_PEER_PROTO_VERSION = 90027;

static const double SMARTVOTING_FILTER_FP_RATE = 0.001;

static const CAmount SMARTVOTING_PROPOSAL_FEE = 100 * COIN;

static const int64_t SMARTVOTING_FEE_CONFIRMATIONS = 6;
static const int64_t SMARTVOTING_MIN_RELAY_FEE_CONFIRMATIONS = 3;
static const int64_t SMARTVOTING_UPDATE_MIN = 60*60;
static const int64_t SMARTVOTING_DELETION_DELAY = 10*60;
static const int64_t SMARTVOTING_ORPHAN_EXPIRATION_TIME = 10*60;

// FOR SEEN MAP ARRAYS - GOVERNANCE OBJECTS AND VOTES
static const int SEEN_OBJECT_IS_VALID = 0;
static const int SEEN_OBJECT_ERROR_INVALID = 1;
static const int SEEN_OBJECT_ERROR_IMMATURE = 2;
static const int SEEN_OBJECT_EXECUTED = 3;
static const int SEEN_OBJECT_UNKNOWN = 4; // the default

typedef std::pair<CProposalVote, int64_t> vote_time_pair_t;

inline bool operator<(const vote_time_pair_t& p1, const vote_time_pair_t& p2)
{
    return (p1.first < p2.first);
}

struct vote_instance_t {

    vote_outcome_enum_t eOutcome;
    int64_t nTime;
    int64_t nCreationTime;

    vote_instance_t(vote_outcome_enum_t eOutcomeIn = VOTE_OUTCOME_NONE, int64_t nTimeIn = 0, int64_t nCreationTimeIn = 0)
        : eOutcome(eOutcomeIn),
          nTime(nTimeIn),
          nCreationTime(nCreationTimeIn)
    {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        int nOutcome = int(eOutcome);
        READWRITE(nOutcome);
        READWRITE(nTime);
        READWRITE(nCreationTime);
        if(ser_action.ForRead()) {
            eOutcome = vote_outcome_enum_t(nOutcome);
        }
    }
};

typedef std::map<int,vote_instance_t> vote_instance_m_t;

typedef vote_instance_m_t::iterator vote_instance_m_it;

typedef vote_instance_m_t::const_iterator vote_instance_m_cit;

struct vote_rec_t {
    vote_instance_m_t mapInstances;

    ADD_SERIALIZE_METHODS

     template <typename Stream, typename Operation>
     inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
     {
         READWRITE(mapInstances);
     }
};

class CProposalMilestone
{
    int64_t nTime;
    uint32_t nAmount;
    std::string strDescription;

public:

    CProposalMilestone() {}
    CProposalMilestone(int64_t nTime, uint32_t nAmount, std::string strDescription):
        nTime(nTime),
        nAmount(nAmount),
        strDescription(strDescription) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nTime);
        READWRITE(nAmount);
        READWRITE(strDescription);
    }

    bool IsDescriptionValid(std::string &strError) const;

    int64_t GetTime() const { return nTime; }
    uint32_t GetAmount() const { return nAmount; }
    std::string GetDescription() const { return strDescription; }

    friend bool operator==(const CProposalMilestone& a, const CProposalMilestone& b)
    {
        return (a.nTime == b.nTime);
    }

    friend bool operator!=(const CProposalMilestone& a, const CProposalMilestone& b)
    {
        return !(a == b);
    }

    friend bool operator<(const CProposalMilestone& a, const CProposalMilestone& b)
    {
        return a.nTime < b.nTime;
    }

};

class CProposal
{

public:
    typedef std::map<CVoteKey, vote_rec_t> vote_m_t;

    typedef vote_m_t::iterator vote_m_it;

    typedef vote_m_t::const_iterator vote_m_cit;

    typedef CacheMultiMap<COutPoint, vote_time_pair_t> vote_cmm_t;

protected:

    std::string title;
    std::string url;
    CSmartAddress address;
    std::vector<CProposalMilestone> vecMilestones;

    /// time this proposal was created
    int64_t nTimeCreated;
    /// time this proposal was marked for deletion
    int64_t nTimeDeletion;

    uint256 nFeeHash;

    /// is valid by blockchain
    bool fCachedLocalValidity;
    std::string strLocalValidityError;

    // VARIOUS FLAGS FOR OBJECT / SET VIA MASTERNODE VOTING

    /// true == minimum network support has been reached for this object to be funded (doesn't mean it will for sure though)
    bool fCachedFunding;

    /// true == minimum network has been reached flagging this proposal as valid and it meets our terms and conditions
    bool fCachedValid;

    /// object was updated and cached values should be updated soon
    bool fDirtyCache;

    /// Object is no longer of interest
    bool fExpired;

    int nCreationHeight;

    vote_m_t mapCurrentVKVotes;

    /// Limited map of votes orphaned by MN
    vote_cmm_t cmmapOrphanVotes;

    CProposalVoteFile fileVotes;

private:
    /// critical section to protect the inner data structures
    mutable CCriticalSection cs;

public:

    CProposal();

    CProposal(const CProposal& other);

    void swap(CProposal& first, CProposal& second);

    static bool ValidateTitle(const std::string &strTitle, std::string &strError);
    static bool CheckURL(const std::string& strURLIn);
    static bool ValidateUrl(const std::string &strUrl, std::string &strError);

    void SetTitle(const std::string &strTitle) { title = strTitle; }
    std::string GetTitle() const { return title; }
    bool IsTitleValid(std::string &strError) const;

    void SetUrl(const std::string &strUrl) { url = strUrl; }
    std::string GetUrl() const { return url; }
    bool IsUrlValid(std::string &strError) const;

    void SetAddress(const CSmartAddress &address) { this->address = address; }
    CSmartAddress GetAddress() const { return address; }
    bool IsAddressValid(std::string &strError) const;

    bool IsMilestoneVectorValid(std::string &strError) const;
    int GetRequestedAmount() const;

    uint256 GetFeeHash() const { return nFeeHash; }

    void SetCreationTime(int64_t nTime) { nTimeCreated = nTime; }
    int64_t GetCreationTime() const { return nTimeCreated; }

    int64_t GetVotingStartHeight() const {
        if( nCreationHeight != -1 )
            return nCreationHeight + SMARTVOTING_FEE_CONFIRMATIONS;
        return nCreationHeight;
    }

    void SetDeletionTime(int64_t nTime) { nTimeDeletion = nTime; }
    int64_t GetDeletionTime() const { return nTimeDeletion; }

    bool IsSetCachedFunding() const {
        return fCachedFunding;
    }

    void SetCachedValid(bool fValid) {
        fCachedValid = fValid;
    }

    bool IsSetCachedValid() const {
        return fCachedValid;
    }

    bool IsSetDirtyCache() const {
        return fDirtyCache;
    }

    bool IsSetExpired() const {
        return fExpired;
    }

    void InvalidateVoteCache() {
        fDirtyCache = true;
    }


    std::vector<CProposalMilestone> GetMilestones() const { return vecMilestones; }

    bool IsValid(std::vector<std::string> &vecErrors) const;
    bool IsValid() const;
    uint256 GetHash() const;

    void Relay(CConnman& connman) const;

    bool ProcessVote(CNode* pfrom,
                     const CProposalVote& vote,
                     CSmartVotingException& exception,
                     CConnman& connman);

    const CProposalVoteFile& GetVoteFile() const {
        return fileVotes;
    }

    void UpdateLocalValidity();
    void UpdateSentinelVariables();

    bool IsValidLocally(std::string& strError, bool fCheckCollateral) const;
    bool IsValidLocally(std::string& strError, int& fMissingConfirmations, bool fCheckCollateral) const;
    bool IsCollateralValid(std::string& strError, int& fMissingConfirmations) const;

    bool UpdateProposalStartHeight();

    void ClearVoteKeyVotes();
    void CheckOrphanVotes(CConnman &connman);

    int64_t GetVotingPower(vote_signal_enum_t eVoteSignalIn, vote_outcome_enum_t eVoteOutcomeIn) const;
    CVoteOutcomes GetVotingPower(const std::set<CVoteKey> &setVoteKeys, vote_signal_enum_t eVoteSignalIn) const;
    CAmount GetAbsoluteYesPower(vote_signal_enum_t eVoteSignalIn) const;
    CAmount GetAbsoluteNoPower(vote_signal_enum_t eVoteSignalIn) const;
    CAmount GetYesPower(vote_signal_enum_t eVoteSignalIn) const;
    CAmount GetNoPower(vote_signal_enum_t eVoteSignalIn) const;
    CAmount GetAbstainPower(vote_signal_enum_t eVoteSignalIn) const;
    CVoteResult GetVotingResult(vote_signal_enum_t eVoteSignalIn) const;
    void GetActiveVoteKeys(std::set<CVoteKey> &setVoteKeys) const;
    bool GetCurrentVKVotes(const CVoteKey &voteKey, vote_rec_t &voteRecord) const;

    int GetValidVoteEndHeight() const;
    int GetFundingVoteEndHeight() const;

    std::string ToString() const;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nTimeCreated);
        READWRITE(title);
        READWRITE(url);
        READWRITE(address);
        READWRITE(vecMilestones);
        READWRITE(nFeeHash);

        if(nType & SER_DISK) {
            // Only include these for the disk file format
            LogPrint("proposal", "CProposal::SerializationOp Reading/writing votes from/to disk\n");
            READWRITE(nTimeDeletion);
            READWRITE(fExpired);
            READWRITE(mapCurrentVKVotes);
            READWRITE(fileVotes);
            LogPrint("proposal", "CProposal::SerializationOp hash = %s, vote count = %d\n", GetHash().ToString(), fileVotes.GetVoteCount());
        }
    }

    CProposal& operator=(CProposal from)
    {
        swap(*this, from);
        return *this;
    }
};

// Used for GUI storage stuff
class CInternalProposal : public CProposal
{

    uint256 hashInternal;
    bool fPaid;
    bool fPublished;

    std::string rawFeeTx;
    std::string strSignedHash;

public:

    CInternalProposal() : CProposal() {}
    CInternalProposal(const CInternalProposal& other);
    CInternalProposal(uint256 hashInternal) : CProposal(), hashInternal(hashInternal) {
        fPaid = false;
        fPublished = false;
    }

    uint256 GetInternalHash() const { return hashInternal; }

    void SetPaid() { fPaid = true; }
    bool IsPaid() const { return fPaid; }

    void SetPublished() { fPublished = true; }
    bool IsPublished() { return fPublished; }

    void SetSignedHash(const std::string& strSigned) { strSignedHash = strSigned; }
    std::string GetSignedHash() const { return strSignedHash; }

    void SetFeeHash(const uint256& nHash) { nFeeHash = nHash; }

    void SetRawFeeTx(const std::string& rawTx) { rawFeeTx = rawTx; }

    void AddMilestone( CProposalMilestone& milestone);
    void RemoveMilestone( size_t index );

    std::string ToString() const;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CProposal*)this);
        READWRITE(hashInternal);
        READWRITE(fPaid);
        READWRITE(fPublished);
        READWRITE(rawFeeTx);
        READWRITE(strSignedHash);
    }

};

#endif // PROPOSAL_H
