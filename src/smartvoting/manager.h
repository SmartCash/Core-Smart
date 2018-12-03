// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTVOTING_MANAGER_H
#define SMARTVOTING_MANAGER_H

#include "bloom.h"
#include "cachemap.h"
#include "cachemultimap.h"
#include "univalue.h"
#include "smartvoting/exceptions.h"
#include "smartvoting/proposal.h"
#include "net.h"


class CSmartVotingManager;

extern CSmartVotingManager smartVoting;

struct ExpirationInfo {
    ExpirationInfo(int64_t _nExpirationTime, int _idFrom) : nExpirationTime(_nExpirationTime), idFrom(_idFrom) {}

    int64_t nExpirationTime;
    NodeId idFrom;
};

typedef std::pair<CProposal, ExpirationInfo> object_info_pair_t;


//
// Governance Manager : Contains all proposals for the budget
//
class CSmartVotingManager
{
    friend class CProposal;

public: // Types

    typedef std::map<uint256, CProposal> proposal_m_t;

    typedef proposal_m_t::iterator proposal_m_it;

    typedef proposal_m_t::const_iterator proposal_m_cit;

    typedef CacheMap<uint256, CProposal*> object_ref_cm_t;

    typedef std::map<uint256, CProposalVote> vote_m_t;

    typedef vote_m_t::iterator vote_m_it;

    typedef vote_m_t::const_iterator vote_m_cit;

    typedef CacheMap<uint256, CProposalVote> vote_cm_t;

    typedef CacheMultiMap<uint256, vote_time_pair_t> vote_cmm_t;

    typedef proposal_m_t::size_type size_type;

    typedef std::map<COutPoint, int> txout_int_m_t;

    typedef std::set<uint256> hash_s_t;

    typedef hash_s_t::iterator hash_s_it;

    typedef hash_s_t::const_iterator hash_s_cit;

    typedef std::map<uint256, object_info_pair_t> object_info_m_t;

    typedef object_info_m_t::iterator object_info_m_it;

    typedef object_info_m_t::const_iterator object_info_m_cit;

    typedef std::map<uint256, int64_t> hash_time_m_t;

    typedef hash_time_m_t::iterator hash_time_m_it;

    typedef hash_time_m_t::const_iterator hash_time_m_cit;

private:
    static const int MAX_CACHE_SIZE = 10000000;

    static const std::string SERIALIZATION_VERSION_STRING;

    static const int MAX_TIME_FUTURE_DEVIATION;
    static const int RELIABLE_PROPAGATION_TIME;

    int64_t nTimeLastDiff;

    // keep track of current block height
    int nCachedBlockHeight;

    // keep track of the scanning errors
    proposal_m_t mapProposals;

    // mapErasedProposals contains key-value pairs, where
    //   key   - governance object's hash
    //   value - expiration time for deleted objects
    hash_time_m_t mapErasedProposals;

    proposal_m_t mapPostponedProposals;
    hash_s_t setAdditionalRelayObjects;

    object_ref_cm_t cmapVoteToProposal;

    vote_cm_t cmapInvalidVotes;

    vote_cmm_t cmmapOrphanVotes;

    hash_s_t setRequestedProposals;

    hash_s_t setRequestedVotes;

    bool fRateChecksEnabled;

    class ScopedLockBool
    {
        bool& ref;
        bool fPrevValue;

    public:
        ScopedLockBool(CCriticalSection& _cs, bool& _ref, bool _value) : ref(_ref)
        {
            AssertLockHeld(_cs);
            fPrevValue = ref;
            ref = _value;
        }

        ~ScopedLockBool()
        {
            ref = fPrevValue;
        }
    };

public:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    CSmartVotingManager();

    virtual ~CSmartVotingManager() {}

    /**
     * This is called by AlreadyHave in net_processing.cpp as part of the inventory
     * retrieval process.  Returns true if we want to retrieve the object, otherwise
     * false. (Note logic is inverted in AlreadyHave).
     */
    bool ConfirmInventoryRequest(const CInv& inv);

    void SyncProposalWithVotes(CNode* pnode, const uint256& nProp, const CBloomFilter& filter, CConnman& connman);
    void SyncAll(CNode* pnode, CConnman& connman) const;

    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman);

    void DoMaintenance(CConnman& connman);

    CProposal* FindProposal(const uint256& nHash);

    // These commands are only used in RPC
    std::vector<CProposalVote> GetMatchingVotes(const uint256& nParentHash) const;
    std::vector<CProposalVote> GetCurrentVotes(const uint256& nParentHash, const COutPoint& mnCollateralOutpointFilter) const;
    std::vector<const CProposal*> GetAllNewerThan(int64_t nMoreThanTime) const;

    void AddProposal(CProposal& proposal, CConnman& connman, CNode* pfrom = NULL);

    void UpdateCachesAndClean();

    void CheckAndRemove() {UpdateCachesAndClean();}

    void Clear()
    {
        LOCK(cs);

        LogPrint("proposal", "SmartVoting manager was cleared\n");
        mapProposals.clear();
        mapErasedProposals.clear();
        cmapVoteToProposal.Clear();
        cmapInvalidVotes.Clear();
        cmmapOrphanVotes.Clear();
    }

    std::string ToString() const;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
        }
        else {
            strVersion = SERIALIZATION_VERSION_STRING;
            READWRITE(strVersion);
        }

        READWRITE(mapErasedProposals);
        READWRITE(cmapInvalidVotes);
        READWRITE(cmmapOrphanVotes);
        READWRITE(mapProposals);
        if(ser_action.ForRead() && (strVersion != SERIALIZATION_VERSION_STRING)) {
            Clear();
            return;
        }
    }

    void UpdatedBlockTip(const CBlockIndex *pindex, CConnman& connman);
    int64_t GetLastDiffTime() const { return nTimeLastDiff; }
    void UpdateLastDiffTime(int64_t nTimeIn) { nTimeLastDiff = nTimeIn; }

    int GetCachedBlockHeight() const { return nCachedBlockHeight; }

    // Accessors for thread-safe access to maps
    bool HaveProposalForHash(const uint256& nHash) const;

    bool HaveVoteForHash(const uint256& nHash) const;

    int GetVoteCount() const;

    bool SerializeProposalForHash(const uint256& nHash, CDataStream& ss) const;

    bool SerializeVoteForHash(const uint256& nHash, CDataStream& ss) const;

    void AddPostponedProposal(const CProposal& proposal)
    {
        LOCK(cs);
        mapPostponedProposals.insert(std::make_pair(proposal.GetHash(), proposal));
    }

    void AddSeenProposal(const uint256& nHash, int status);

    void AddSeenVote(const uint256& nHash, int status);

    bool ProcessVoteAndRelay(const CProposalVote& vote, CSmartVotingException& exception, CConnman& connman);
    bool ProcessVoteAndRelay(const CProposalVote& vote, std::string &strError, CConnman& connman);

    void CheckMasternodeOrphanVotes(CConnman& connman);

    void CheckMasternodeOrphanObjects(CConnman& connman);

    void CheckPostponedProposals(CConnman& connman);

    bool AreRateChecksEnabled() const {
        LOCK(cs);
        return fRateChecksEnabled;
    }

    void InitOnLoad();

    int RequestProposalVotes(CNode* pnode, CConnman& connman);
    int RequestProposalVotes(const std::vector<CNode*>& vNodesCopy, CConnman& connman);

    UniValue ToJson() const;
private:
    void RequestProposal(CNode* pfrom, const uint256& nHash, CConnman& connman, bool fUseFilter = false);

    void AddInvalidVote(const CProposalVote& vote)
    {
        cmapInvalidVotes.Insert(vote.GetHash(), vote);
    }

    void AddOrphanVote(const CProposalVote& vote)
    {
        cmmapOrphanVotes.Insert(vote.GetHash(), vote_time_pair_t(vote, GetAdjustedTime() + SMARTVOTING_ORPHAN_EXPIRATION_TIME));
    }

    bool ProcessVote(CNode* pfrom, const CProposalVote& vote, CSmartVotingException& exception, CConnman& connman);

    /// Called to indicate a requested object has been received
    bool AcceptProposalMessage(const uint256& nHash);

    /// Called to indicate a requested vote has been received
    bool AcceptVoteMessage(const uint256& nHash);

    static bool AcceptMessage(const uint256& nHash, hash_s_t& setHash);

    void CheckOrphanVotes(CProposal& proposal, CSmartVotingException& exception, CConnman& connman);

    void RebuildIndexes();

    void AddCachedTriggers();

    void RequestOrphanProposals(CConnman& connman);

    void CleanOrphanObjects();

};



#endif // SMARTVOTING_MANAGER_H
