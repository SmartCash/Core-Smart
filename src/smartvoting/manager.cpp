// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/validation.h"
#include "smartnode/netfulfilledman.h"
#include "random.h"
#include "smartvoting/manager.h"
#include "smartnode/smartnodesync.h"
#include "validation.h"

CSmartVotingManager smartVoting;

const std::string CSmartVotingManager::SERIALIZATION_VERSION_STRING = "CSmartVotingManager-Version-1";
const int CSmartVotingManager::MAX_TIME_FUTURE_DEVIATION = 60*60;
const int CSmartVotingManager::RELIABLE_PROPAGATION_TIME = 60;

CSmartVotingManager::CSmartVotingManager()
    : nTimeLastDiff(0),
      nCachedBlockHeight(0),
      mapProposals(),
      mapErasedProposals(),
      cmapVoteToProposal(MAX_CACHE_SIZE),
      cmapInvalidVotes(MAX_CACHE_SIZE),
      cmmapOrphanVotes(MAX_CACHE_SIZE),
      setRequestedProposals(),
      setRequestedVotes(),
      fRateChecksEnabled(true),
      cs()
{}

// Accessors for thread-safe access to maps
bool CSmartVotingManager::HaveProposalForHash(const uint256& nHash) const
{
    LOCK(cs);
    return (mapProposals.count(nHash) == 1 || mapPostponedProposals.count(nHash) == 1);
}

bool CSmartVotingManager::SerializeProposalForHash(const uint256& nHash, CDataStream& ss) const
{
    LOCK(cs);
    proposal_m_cit it = mapProposals.find(nHash);
    if (it == mapProposals.end()) {
        it = mapPostponedProposals.find(nHash);
        if (it == mapPostponedProposals.end())
            return false;
    }
    ss << it->second;
    return true;
}

bool CSmartVotingManager::HaveVoteForHash(const uint256& nHash) const
{
    LOCK(cs);

    CProposal* pProposal = NULL;
    return cmapVoteToProposal.Get(nHash, pProposal) && pProposal->GetVoteFile().HasVote(nHash);
}

int CSmartVotingManager::GetVoteCount() const
{
    LOCK(cs);
    return (int)cmapVoteToProposal.GetSize();
}

bool CSmartVotingManager::SerializeVoteForHash(const uint256& nHash, CDataStream& ss) const
{
    LOCK(cs);

    CProposal* pProposal = NULL;
    return cmapVoteToProposal.Get(nHash,pProposal) && pProposal->GetVoteFile().SerializeVoteToStream(nHash, ss);
}

bool CSmartVotingManager::ProcessVoteAndRelay(const CProposalVote &vote, CSmartVotingException &exception, CConnman &connman){
    bool fOK = ProcessVote(NULL, vote, exception, connman);
    if(fOK) {
        vote.Relay(connman);
    }
    return fOK;
}

bool CSmartVotingManager::ProcessVoteAndRelay(const CProposalVote &vote, string &strError, CConnman &connman)
{
    CSmartVotingException exception;
    bool fOK = ProcessVote(NULL, vote, exception, connman);
    if(fOK) {
        vote.Relay(connman);
    }else{
        strError = exception.GetMessage();
    }
    return fOK;
}

void CSmartVotingManager::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    // lite mode is not supported
    if( fLiteMode ) return;
    if( !smartnodeSync.IsBlockchainSynced() ) return;
    if( MainNet() && chainActive.Height() < SMARTVOTING_START_HEIGHT ) return;

    // ANOTHER CLIENT IS ASKING US TO HELP THEM SYNC PROPOSAL DATA
    if (strCommand == NetMsgType::VOTINGSYNC)
    {

        if(pfrom->nVersion < MIN_VOTING_PEER_PROTO_VERSION) {
            LogPrint("proposal", "VOTINGSYNC -- peer=%d using obsolete version %i\n", pfrom->id, pfrom->nVersion);
            connman.PushMessage(pfrom, NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_VOTING_PEER_PROTO_VERSION));
            return;
        }

        // Ignore such requests until we are fully synced.
        // We could start processing this after smartnode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!smartnodeSync.IsSynced()) return;

        uint256 nProp;
        CBloomFilter filter;

        vRecv >> nProp;
        vRecv >> filter;

        filter.UpdateEmptyFull();

        if(nProp == uint256()) {
            SyncAll(pfrom, connman);
        } else {
            SyncProposalWithVotes(pfrom, nProp, filter, connman);
        }
        LogPrint("proposal", "VOTINGSYNC -- syncing proposals to our peer at %s\n", pfrom->addr.ToString());
    }

    // A NEW GOVERNANCE OBJECT HAS ARRIVED
    else if (strCommand == NetMsgType::VOTINGPROPOSAL)
    {
        // MAKE SURE WE HAVE A VALID REFERENCE TO THE TIP BEFORE CONTINUING

        CProposal proposal;
        vRecv >> proposal;

        uint256 nHash = proposal.GetHash();

        pfrom->setAskFor.erase(nHash);

        if(pfrom->nVersion < MIN_VOTING_PEER_PROTO_VERSION) {
            LogPrint("proposal", "VOTINGPROPOSAL -- peer=%d using obsolete version %i\n", pfrom->id, pfrom->nVersion);
            connman.PushMessage(pfrom, NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_VOTING_PEER_PROTO_VERSION));
            return;
        }

        if(!smartnodeSync.IsSmartnodeListSynced()) {
            LogPrint("proposal", "VOTINGPROPOSAL -- smartnode list not synced\n");
            return;
        }

        std::string strHash = nHash.ToString();

        LogPrint("proposal", "VOTINGPROPOSAL -- Received proposal: %s\n", strHash);

        if(!AcceptProposalMessage(nHash)) {
            LogPrintf("VOTINGPROPOSAL -- Received unrequested proposal: %s\n", strHash);
            return;
        }

        LOCK2(cs_main, cs);

        if(mapProposals.count(nHash) || mapPostponedProposals.count(nHash) ||
           mapErasedProposals.count(nHash)) {
            LogPrint("proposal", "VOTINGPROPOSAL -- Received already seen object: %s\n", strHash);
            return;
        }

        std::string strError = "";
        // CHECK PROPOSAL AGAINST LOCAL BLOCKCHAIN

        int fMissingConfirmations;
        bool fIsValid = proposal.IsValidLocally(strError, fMissingConfirmations, true);

        if(!fIsValid) {
            if(fMissingConfirmations > 0) {
                AddPostponedProposal(proposal);
                LogPrintf("VOTINGPROPOSAL -- Not enough fee confirmations for: %s, strError = %s\n", strHash, strError);
            } else {
                LogPrintf("VOTINGPROPOSAL -- Governance object is invalid - %s\n", strError);
                // apply node's ban score
                Misbehaving(pfrom->GetId(), 20);
            }

            return;
        }

        AddProposal(proposal, connman, pfrom);
    }

    // A NEW PROPOSAL VOTE HAS ARRIVED
    else if (strCommand == NetMsgType::VOTINGPROPOSALVOTE)
    {
        CProposalVote vote;
        vRecv >> vote;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        if(pfrom->nVersion < MIN_VOTING_PEER_PROTO_VERSION) {
            LogPrint("proposal", "VOTINGPROPOSALVOTE -- peer=%d using obsolete version %i\n", pfrom->id, pfrom->nVersion);
            connman.PushMessage(pfrom, NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_VOTING_PEER_PROTO_VERSION));
        }

        // Ignore such messages until smartnode list is synced
        if(!smartnodeSync.IsSmartnodeListSynced()) {
            LogPrint("proposal", "VOTINGPROPOSALVOTE -- smartnode list not synced\n");
            return;
        }

        LogPrint("proposal", "VOTINGPROPOSALVOTE -- Received vote: %s\n", vote.ToString());

        std::string strHash = nHash.ToString();

        if(!AcceptVoteMessage(nHash)) {
            LogPrint("proposal", "VOTINGPROPOSALVOTE -- Received unrequested vote: %s, hash: %s, peer = %d\n",
                      vote.ToString(), strHash, pfrom->GetId());
            return;
        }

        CSmartVotingException exception;
        if(ProcessVote(pfrom, vote, exception, connman)) {
            LogPrint("proposal", "VOTINGPROPOSALVOTE -- %s new\n", strHash);
            smartnodeSync.BumpAssetLastTime("VOTINGPROPOSALVOTE");
            vote.Relay(connman);
        }
        else {
            LogPrint("proposal", "VOTINGPROPOSALVOTE -- Rejected vote, error = %s\n", exception.what());
            if((exception.GetNodePenalty() != 0) && smartnodeSync.IsSynced()) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), exception.GetNodePenalty());
            }
            return;
        }

    }
}

void CSmartVotingManager::CheckOrphanVotes(CProposal& proposal, CSmartVotingException& exception, CConnman& connman)
{
    uint256 nHash = proposal.GetHash();
    std::vector<vote_time_pair_t> vecVotePairs;
    cmmapOrphanVotes.GetAll(nHash, vecVotePairs);

    ScopedLockBool guard(cs, fRateChecksEnabled, false);

    int64_t nNow = GetAdjustedTime();
    for(size_t i = 0; i < vecVotePairs.size(); ++i) {
        bool fRemove = false;
        vote_time_pair_t& pairVote = vecVotePairs[i];
        CProposalVote& vote = pairVote.first;
        CSmartVotingException exception;
        if(pairVote.second < nNow) {
            fRemove = true;
        }
        else if(proposal.ProcessVote(NULL, vote, exception, connman)) {
            vote.Relay(connman);
            fRemove = true;
        }
        if(fRemove) {
            cmmapOrphanVotes.Erase(nHash, pairVote);
        }
    }
}

void CSmartVotingManager::AddProposal(CProposal& proposal, CConnman& connman, CNode* pfrom)
{
    DBG( std::cout << "CSmartVotingManager::AddProposal START" << std::endl; );

    uint256 nHash = proposal.GetHash();
    std::string strHash = nHash.ToString();

    LOCK2(cs_main, cs);
    std::vector<std::string> vecErrors;

    // MAKE SURE THIS OBJECT IS OK

    if(!proposal.IsValid(vecErrors)) {

        std::string strError;
        for( auto error : vecErrors ) strError += error + ",";

        LogPrintf("CSmartVotingManager::AddProposal -- invalid governance object - %s - (nCachedBlockHeight %d) \n", strError, nCachedBlockHeight);
        return;
    }

    //TODO check collateral
//    std::string strError;
//    if( !proposal.IsValidLocally(strError,) ){

//    }

    LogPrint("proposal", "CSmartVotingManager::AddProposal -- Adding proposal: hash = %s\n", nHash.ToString());

    // INSERT INTO OUR GOVERNANCE OBJECT MEMORY
    // IF WE HAVE THIS OBJECT ALREADY, WE DON'T WANT ANOTHER COPY
    auto objpair = mapProposals.emplace(nHash, proposal);

    if(!objpair.second) {
        LogPrintf("CSmartVotingManager::AddProposal -- already have governance object %s\n", nHash.ToString());
        return;
    }

    // SHOULD WE ADD THIS OBJECT TO ANY OTHER MANANGERS?

    DBG( std::cout << "CSmartVotingManager::AddProposal Before trigger block, GetDataAsPlainString = "
              << proposal.GetDataAsPlainString()
              << ", nObjectType = " << proposal.nObjectType
              << std::endl; );


    LogPrintf("CSmartVotingManager::AddProposal -- %s new, received from %s\n", strHash, pfrom? pfrom->GetAddrName() : "NULL");
    proposal.Relay(connman);

//    // Update the rate buffer
//    MasternodeRateUpdate(proposal);

    smartnodeSync.BumpAssetLastTime("CSmartVotingManager::AddProposal");

    // WE MIGHT HAVE PENDING/ORPHAN VOTES FOR THIS OBJECT

    CSmartVotingException exception;
    CheckOrphanVotes(proposal, exception, connman);

    DBG( std::cout << "CSmartVotingManager::AddProposal END" << std::endl; );
}

void CSmartVotingManager::UpdateCachesAndClean()
{
    LogPrint("proposal", "CSmartVotingManager::UpdateCachesAndClean\n");

    LOCK2(cs_main, cs);

    ScopedLockBool guard(cs, fRateChecksEnabled, false);

    proposal_m_it it = mapProposals.begin();
    int64_t nNow = GetAdjustedTime();

    while(it != mapProposals.end())
    {
        CProposal* pProposal = &((*it).second);

        if(!pProposal) {
            ++it;
            continue;
        }

        uint256 nHash = it->first;
        std::string strHash = nHash.ToString();

        // UPDATE SENTINEL SIGNALING VARIABLES
        pProposal->UpdateSentinelVariables();

        // IF CACHE IS NOT DIRTY, WHY DO THIS?
        if(pProposal->IsSetDirtyCache()) {
            // UPDATE LOCAL VALIDITY AGAINST CRYPTO DATA
            pProposal->UpdateLocalValidity();
        }

        // IF DELETE=TRUE, THEN CLEAN THE MESS UP!

        int64_t nTimeSinceDeletion = nNow - pProposal->GetDeletionTime();

        LogPrint("proposal", "CSmartVotingManager::UpdateCachesAndClean -- Checking object for deletion: %s, deletion time = %d, time since deletion = %d, valid flag = %d, expired flag = %d\n",
                 strHash, pProposal->GetDeletionTime(), nTimeSinceDeletion, pProposal->IsSetCachedValid(), pProposal->IsSetExpired());

        if((!pProposal->IsSetCachedValid() || pProposal->IsSetExpired()) &&
           (nTimeSinceDeletion >= SMARTVOTING_DELETION_DELAY)) {
            LogPrintf("CSmartVotingManager::UpdateCachesAndClean -- erase proposal %s\n", (*it).first.ToString());

            // Remove vote references
            const object_ref_cm_t::list_t& listItems = cmapVoteToProposal.GetItemList();
            object_ref_cm_t::list_cit lit = listItems.begin();
            while(lit != listItems.end()) {
                if(lit->value == pProposal) {
                    uint256 nKey = lit->key;
                    ++lit;
                    cmapVoteToProposal.Erase(nKey);
                }
                else {
                    ++lit;
                }
            }

            int64_t nTimeExpired{0};

            // keep hashes of deleted proposals forever
            nTimeExpired = std::numeric_limits<int64_t>::max();

            mapErasedProposals.insert(std::make_pair(nHash, nTimeExpired));
            mapProposals.erase(it++);
        } else {

            if (!pProposal->IsValid()) {
                LogPrintf("CSmartVotingManager::UpdateCachesAndClean -- set for deletion expired obj %s\n", (*it).first.ToString());
                pProposal->SetCachedValid(false);
                if (!pProposal->GetDeletionTime()) {
                    pProposal->SetDeletionTime(nNow);
                }
            }

            ++it;
        }
    }

    // forget about expired deleted objects
    hash_time_m_it s_it = mapErasedProposals.begin();
    while(s_it != mapErasedProposals.end()) {
        if(s_it->second < nNow)
            mapErasedProposals.erase(s_it++);
        else
            ++s_it;
    }

    LogPrintf("CSmartVotingManager::UpdateCachesAndClean -- %s\n", ToString());
}

CProposal* CSmartVotingManager::FindProposal(const uint256& nHash)
{
    LOCK(cs);

    if(mapProposals.count(nHash))
        return &mapProposals[nHash];

    return NULL;
}

std::vector<CProposalVote> CSmartVotingManager::GetMatchingVotes(const uint256& nParentHash) const
{
    LOCK(cs);
    std::vector<CProposalVote> vecResult;

    proposal_m_cit it = mapProposals.find(nParentHash);
    if(it == mapProposals.end()) {
        return vecResult;
    }

    return it->second.GetVoteFile().GetVotes();
}

std::vector<CProposalVote> CSmartVotingManager::GetCurrentVotes(const uint256& nParentHash, const COutPoint& mnCollateralOutpointFilter) const
{
    LOCK(cs);
    std::vector<CProposalVote> vecResult;

//    // Find the governance object or short-circuit.
//    proposal_m_cit it = mapProposals.find(nParentHash);
//    if(it == mapProposals.end()) return vecResult;
//    const CProposal& proposal = it->second;

//    CMasternode mn;
//    std::map<COutPoint, CMasternode> mapMasternodes;
//    if(mnCollateralOutpointFilter.IsNull()) {
//        mapMasternodes = mnodeman.GetFullMasternodeMap();
//    } else if (mnodeman.Get(mnCollateralOutpointFilter, mn)) {
//        mapMasternodes[mnCollateralOutpointFilter] = mn;
//    }

//    // Loop thru each MN collateral outpoint and get the votes for the `nParentHash` governance object
//    for (const auto& mnpair : mapMasternodes)
//    {
//        // get a vote_rec_t from the proposal
//        vote_rec_t voteRecord;
//        if (!proposal.GetCurrentMNVotes(mnpair.first, voteRecord)) continue;

//        for (vote_instance_m_it it3 = voteRecord.mapInstances.begin(); it3 != voteRecord.mapInstances.end(); ++it3) {
//            int signal = (it3->first);
//            int outcome = ((it3->second).eOutcome);
//            int64_t nCreationTime = ((it3->second).nCreationTime);

//            CProposalVote vote = CProposalVote(mnpair.first, nParentHash, (vote_signal_enum_t)signal, (vote_outcome_enum_t)outcome);
//            vote.SetTime(nCreationTime);

//            vecResult.push_back(vote);
//        }
//    }

    return vecResult;
}

std::vector<const CProposal*> CSmartVotingManager::GetAllNewerThan(int64_t nMoreThanTime) const
{
    LOCK(cs);

    std::vector<const CProposal*> vGovObjs;

    proposal_m_cit it = mapProposals.begin();
    while(it != mapProposals.end())
    {
        // IF THIS OBJECT IS OLDER THAN TIME, CONTINUE

        if((*it).second.GetCreationTime() < nMoreThanTime) {
            ++it;
            continue;
        }

        // ADD GOVERNANCE OBJECT TO LIST

        const CProposal* pProposal = &((*it).second);
        vGovObjs.push_back(pProposal);

        // NEXT

        ++it;
    }

    return vGovObjs;
}

//
// Sort by votes, if there's a tie sort by their feeHash TX
//
struct sortProposalsByVotes {
    bool operator()(const std::pair<CProposal*, int> &left, const std::pair<CProposal*, int> &right) {
        if (left.second != right.second)
            return (left.second > right.second);
        return (UintToArith256(left.first->GetFeeHash()) > UintToArith256(right.first->GetFeeHash()));
    }
};

void CSmartVotingManager::DoMaintenance(CConnman& connman)
{
    if(fLiteMode || !smartnodeSync.IsSynced()) return;

    // CHECK OBJECTS WE'VE ASKED FOR, REMOVE OLD ENTRIES

    CleanOrphanObjects();

    RequestOrphanProposals(connman);

    // CHECK AND REMOVE - REPROCESS GOVERNANCE OBJECTS

    UpdateCachesAndClean();
}

bool CSmartVotingManager::ConfirmInventoryRequest(const CInv& inv)
{
    // do not request objects until it's time to sync
    if(!smartnodeSync.IsWinnersListSynced()) return false;

    LOCK(cs);

    LogPrint("proposal", "CSmartVotingManager::ConfirmInventoryRequest inv = %s\n", inv.ToString());

    // First check if we've already recorded this object
    switch(inv.type) {
    case MSG_VOTING_PROPOSAL:
    {
        if(mapProposals.count(inv.hash) == 1 || mapPostponedProposals.count(inv.hash) == 1) {
            LogPrint("proposal", "CSmartVotingManager::ConfirmInventoryRequest already have proposal, returning false\n");
            return false;
        }
    }
    break;
    case MSG_VOTING_PROPOSAL_VOTE:
    {
        if(cmapVoteToProposal.HasKey(inv.hash)) {
            LogPrint("proposal", "CSmartVotingManager::ConfirmInventoryRequest already have governance vote, returning false\n");
            return false;
        }
    }
    break;
    default:
        LogPrint("proposal", "CSmartVotingManager::ConfirmInventoryRequest unknown type, returning false\n");
        return false;
    }


    hash_s_t* setHash = NULL;
    switch(inv.type) {
    case MSG_VOTING_PROPOSAL:
        setHash = &setRequestedProposals;
        break;
    case MSG_VOTING_PROPOSAL_VOTE:
        setHash = &setRequestedVotes;
        break;
    default:
        return false;
    }

    hash_s_cit it = setHash->find(inv.hash);
    if(it == setHash->end()) {
        setHash->insert(inv.hash);
        LogPrint("proposal", "CSmartVotingManager::ConfirmInventoryRequest added inv to requested set\n");
    }

    LogPrint("proposal", "CSmartVotingManager::ConfirmInventoryRequest reached end, returning true\n");
    return true;
}

void CSmartVotingManager::SyncProposalWithVotes(CNode* pnode, const uint256& nProp, const CBloomFilter& filter, CConnman& connman)
{
    // do not provide any data until our node is synced
    if(!smartnodeSync.IsSynced()) return;

    int nVoteCount = 0;

    // SYNC GOVERNANCE OBJECTS WITH OTHER CLIENT

    LogPrint("proposal", "CSmartVotingManager::%s -- syncing single object to peer=%d, nProp = %s\n", __func__, pnode->id, nProp.ToString());

    LOCK2(cs_main, cs);

    // single valid object and its valid votes
    proposal_m_it it = mapProposals.find(nProp);
    if(it == mapProposals.end()) {
        LogPrint("proposal", "CSmartVotingManager::%s -- no matching object for hash %s, peer=%d\n", __func__, nProp.ToString(), pnode->id);
        return;
    }
    CProposal& proposal = it->second;
    std::string strHash = it->first.ToString();

    LogPrint("proposal", "CSmartVotingManager::%s -- attempting to sync proposal: %s, peer=%d\n", __func__, strHash, pnode->id);

    if(!proposal.IsSetCachedValid() || proposal.IsSetExpired()) {
        LogPrintf("CSmartVotingManager::%s -- not syncing deleted/expired proposal: %s, peer=%d\n", __func__,
                  strHash, pnode->id);
        return;
    }

    // Push the proposal inventory message over to the other client
    LogPrint("proposal", "CSmartVotingManager::%s -- syncing proposal: %s, peer=%d\n", __func__, strHash, pnode->id);
    pnode->PushInventory(CInv(MSG_VOTING_PROPOSAL, it->first));

    auto fileVotes = proposal.GetVoteFile();
    std::string strError;
    for (const auto& vote : fileVotes.GetVotes()) {
        uint256 nVoteHash = vote.GetHash();
        if(filter.contains(nVoteHash) || !vote.IsValid(true, true, strError)) {
            continue;
        }
        pnode->PushInventory(CInv(MSG_VOTING_PROPOSAL_VOTE, nVoteHash));
        ++nVoteCount;
    }

    connman.PushMessage(pnode, NetMsgType::SYNCSTATUSCOUNT, SMARTNODE_SYNC_PROPOSAL, 1);
    connman.PushMessage(pnode, NetMsgType::SYNCSTATUSCOUNT, SMARTNODE_SYNC_PROPOSAL_VOTE, nVoteCount);
    LogPrintf("CSmartVotingManager::%s -- sent 1 object and %d votes to peer=%d\n", __func__, nVoteCount, pnode->id);
}

void CSmartVotingManager::SyncAll(CNode* pnode, CConnman& connman) const
{
    // do not provide any data until our node is synced
    if(!smartnodeSync.IsSynced()) return;

    if(netfulfilledman.HasFulfilledRequest(pnode->addr, NetMsgType::VOTINGSYNC)) {
        LOCK(cs_main);
        // Asking for the whole list multiple times in a short period of time is no good
        LogPrint("proposal", "CSmartVotingManager::%s -- peer already asked me for the list\n", __func__);
        Misbehaving(pnode->GetId(), 20);
        return;
    }
    netfulfilledman.AddFulfilledRequest(pnode->addr, NetMsgType::VOTINGSYNC);

    int nObjCount = 0;
    int nVoteCount = 0;

    // SYNC GOVERNANCE OBJECTS WITH OTHER CLIENT

    LogPrint("proposal", "CSmartVotingManager::%s -- syncing all proposals to peer=%d\n", __func__, pnode->id);

    LOCK2(cs_main, cs);

    // all valid objects, no votes
    for(proposal_m_cit it = mapProposals.begin(); it != mapProposals.end(); ++it) {
        const CProposal& proposal = it->second;
        std::string strHash = it->first.ToString();

        LogPrint("proposal", "CSmartVotingManager::%s -- attempting to sync proposal: %s, peer=%d\n", __func__, strHash, pnode->id);

        if( !proposal.IsSetCachedValid() || proposal.IsSetExpired()) {
            LogPrintf("CSmartVotingManager::%s -- not syncing deleted/expired proposal: %s, peer=%d\n", __func__,
                      strHash, pnode->id);
            continue;
        }

        // Push the inventory budget proposal message over to the other client
        LogPrint("proposal", "CSmartVotingManager::%s -- syncing proposal: %s, peer=%d\n", __func__, strHash, pnode->id);
        pnode->PushInventory(CInv(MSG_VOTING_PROPOSAL, it->first));
        ++nObjCount;
    }

    connman.PushMessage(pnode, NetMsgType::SYNCSTATUSCOUNT, SMARTNODE_SYNC_PROPOSAL, nObjCount);
    connman.PushMessage(pnode, NetMsgType::SYNCSTATUSCOUNT, SMARTNODE_SYNC_PROPOSAL_VOTE, nVoteCount);
    LogPrintf("CSmartVotingManager::%s -- sent %d proposals and %d votes to peer=%d\n", __func__, nObjCount, nVoteCount, pnode->id);
}

bool CSmartVotingManager::ProcessVote(CNode* pfrom, const CProposalVote& vote, CSmartVotingException& exception, CConnman& connman)
{
    ENTER_CRITICAL_SECTION(cs);
    uint256 nHashVote = vote.GetHash();
    uint256 nHashProposal = vote.GetProposalHash();

    if(cmapVoteToProposal.HasKey(nHashVote)) {
        std::ostringstream ostr;
        ostr << "Old invalid vote "
                << ", skipping known valid vote = " << nHashVote.ToString()
                << ", proposal hash = " << nHashProposal.ToString();
        LogPrint("proposal","CSmartVotingManager::ProcessVote -- %s\n", ostr.str());
        exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_TEMPORARY_ERROR);
        LEAVE_CRITICAL_SECTION(cs);
        return false;
    }

    if(cmapInvalidVotes.HasKey(nHashVote)) {
        std::ostringstream ostr;
        ostr << "Old invalid vote "
                << ", votekey = " << vote.GetVoteKey().ToString()
                << ", proposal hash = " << nHashProposal.ToString();
        LogPrint("proposal", "CSmartVotingManager::ProcessVote -- %s\n", ostr.str());
        exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_PERMANENT_ERROR, 20);
        LEAVE_CRITICAL_SECTION(cs);
        return false;
    }

    proposal_m_it it = mapProposals.find(nHashProposal);
    if(it == mapProposals.end()) {
        std::ostringstream ostr;
        ostr << "Unknown proposal " << nHashProposal.ToString()
             << ", votekey = " << vote.GetVoteKey().ToString();
        exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_WARNING);
        if(cmmapOrphanVotes.Insert(nHashProposal, vote_time_pair_t(vote, GetAdjustedTime() + SMARTVOTING_ORPHAN_EXPIRATION_TIME))) {
            LEAVE_CRITICAL_SECTION(cs);
            RequestProposal(pfrom, nHashProposal, connman);
            LogPrint("proposal", "CSmartVotingManager::ProcessVote -- %s\n", ostr.str());
            return false;
        }

        LogPrint("proposal", "%s\n", ostr.str());
        LEAVE_CRITICAL_SECTION(cs);
        return false;
    }

    CProposal& proposal = it->second;

    if(!proposal.IsSetCachedValid() || proposal.IsSetExpired()) {
        std::ostringstream ostr;
        ostr << "Ignoring vote for expired or invalid proposal " << nHashProposal.ToString()
             << ", votekey = " << vote.GetVoteKey().ToString();
        exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_WARNING);
        LogPrint("proposal", "CSmartVotingManager::ProcessVote -- %s\n", ostr.str());
        LEAVE_CRITICAL_SECTION(cs);
        return false;
    }

    bool fOk = proposal.ProcessVote(pfrom, vote, exception, connman) && cmapVoteToProposal.Insert(nHashVote, &proposal);
    LEAVE_CRITICAL_SECTION(cs);
    return fOk;
}

void CSmartVotingManager::CheckMasternodeOrphanVotes(CConnman& connman)
{
    LOCK2(cs_main, cs);

    ScopedLockBool guard(cs, fRateChecksEnabled, false);

    for(proposal_m_it it = mapProposals.begin(); it != mapProposals.end(); ++it) {
        it->second.CheckOrphanVotes(connman);
    }
}

void CSmartVotingManager::CheckPostponedProposals(CConnman& connman)
{
    if(!smartnodeSync.IsSynced()) return;

    LOCK2(cs_main, cs);

    // Check postponed proposals
    for(proposal_m_it it = mapPostponedProposals.begin(); it != mapPostponedProposals.end();) {

        const uint256& nHash = it->first;
        CProposal& proposal = it->second;

        std::string strError;
        int fMissingConfirmations;
        if (proposal.IsCollateralValid(strError, fMissingConfirmations))
        {
            if(proposal.IsValidLocally(strError, false))
                AddProposal(proposal, connman);
            else
                LogPrintf("CSmartVotingManager::CheckPostponedProposals -- %s invalid\n", nHash.ToString());

        } else if(fMissingConfirmations > 0) {
            // wait for more confirmations
            ++it;
            continue;
        }

        // remove processed or invalid object from the queue
        mapPostponedProposals.erase(it++);
    }


    // Perform additional relays for triggers
    int64_t nNow = GetAdjustedTime();

    for(hash_s_it it = setAdditionalRelayObjects.begin(); it != setAdditionalRelayObjects.end();) {

        proposal_m_it itObject = mapProposals.find(*it);
        if(itObject != mapProposals.end()) {

            CProposal& proposal = itObject->second;

            int64_t nTimestamp = proposal.GetCreationTime();

            bool fValid = (nTimestamp <= nNow + MAX_TIME_FUTURE_DEVIATION); // TODO proposal end?
            bool fReady = (nTimestamp <= nNow + MAX_TIME_FUTURE_DEVIATION - RELIABLE_PROPAGATION_TIME);

            if(fValid) {
                if(fReady) {
                    LogPrintf("CSmartVotingManager::CheckPostponedProposals -- additional relay: hash = %s\n", proposal.GetHash().ToString());
                    proposal.Relay(connman);
                } else {
                    it++;
                    continue;
                }
            }

        } else {
            LogPrintf("CSmartVotingManager::CheckPostponedProposals -- additional relay of unknown object: %s\n", it->ToString());
        }

        setAdditionalRelayObjects.erase(it++);
    }
}

void CSmartVotingManager::RequestProposal(CNode* pfrom, const uint256& nHash, CConnman& connman, bool fUseFilter)
{
    if(!pfrom) {
        return;
    }

    LogPrint("proposal", "CProposal::RequestGovernanceObject -- hash = %s (peer=%d)\n", nHash.ToString(), pfrom->GetId());

    CBloomFilter filter;
    filter.clear();

    int nVoteCount = 0;
    if(fUseFilter) {
        LOCK(cs);
        CProposal* pProposal = FindProposal(nHash);

        if(pProposal) {
            filter = CBloomFilter(Params().GetConsensus().nVotingFilterElements, SMARTVOTING_FILTER_FP_RATE, GetRandInt(999999), BLOOM_UPDATE_ALL);
            std::vector<CProposalVote> vecVotes = pProposal->GetVoteFile().GetVotes();
            nVoteCount = vecVotes.size();
            for(size_t i = 0; i < vecVotes.size(); ++i) {
                filter.insert(vecVotes[i].GetHash());
            }
        }
    }

    LogPrint("proposal", "CSmartVotingManager::RequestGovernanceObject -- nHash %s nVoteCount %d peer=%d\n", nHash.ToString(), nVoteCount, pfrom->id);
    connman.PushMessage(pfrom, NetMsgType::VOTINGSYNC, nHash, filter);
}

int CSmartVotingManager::RequestProposalVotes(CNode* pnode, CConnman& connman)
{
    if(pnode->nVersion < MIN_VOTING_PEER_PROTO_VERSION) return -3;
    std::vector<CNode*> vNodesCopy;
    vNodesCopy.push_back(pnode);
    return RequestProposalVotes(vNodesCopy, connman);
}

int CSmartVotingManager::RequestProposalVotes(const std::vector<CNode*>& vNodesCopy, CConnman& connman)
{
    static std::map<uint256, std::map<CService, int64_t> > mapAskedRecently;

    if(vNodesCopy.empty()) return -1;

    int64_t nNow = GetTime();
    int nTimeout = 60 * 60;
    size_t nPeersPerHashMax = 3;

    std::vector<CProposal*> vecProposalsTemp;

    // This should help us to get some idea about an impact this can bring once deployed on mainnet.
    // Testnet is ~40 times smaller in masternode count, but only ~1000 masternodes usually vote,
    // so 1 obj on mainnet == ~10 objs or ~1000 votes on testnet. However we want to test a higher
    // number of votes to make sure it's robust enough, so aim at 2000 votes per masternode per request.
    // On mainnet nMaxObjRequestsPerNode is always set to 1.
    int nMaxProposalRequestsPerNode = 1;
    size_t nProjectedVotes = 10000;
//    if(Params().NetworkIDString() != CBaseChainParams::MAIN) {
//        nMaxProposalRequestsPerNode = std::max(1, int(nProjectedVotes / std::max(1, mnodeman.size())));
//    }

    {
        LOCK2(cs_main, cs);

        if(mapProposals.empty()) return -2;

        for(proposal_m_it it = mapProposals.begin(); it != mapProposals.end(); ++it) {
            if(mapAskedRecently.count(it->first)) {
                std::map<CService, int64_t>::iterator it1 = mapAskedRecently[it->first].begin();
                while(it1 != mapAskedRecently[it->first].end()) {
                    if(it1->second < nNow) {
                        mapAskedRecently[it->first].erase(it1++);
                    } else {
                        ++it1;
                    }
                }
                if(mapAskedRecently[it->first].size() >= nPeersPerHashMax) continue;
            }

            vecProposalsTemp.push_back(&(it->second));
        }
    }

    LogPrint("proposal", "CSmartVotingManager::RequestProposalVotes -- start: vecProposalsTemp %d mapAskedRecently %d\n",
                vecProposalsTemp.size(), mapAskedRecently.size());

    FastRandomContext insecure_rand;
    // shuffle pointers
    std::random_shuffle(vecProposalsTemp.begin(), vecProposalsTemp.end(), insecure_rand);

    for (int i = 0; i < nMaxProposalRequestsPerNode; ++i) {
        uint256 nHashProposal;

        if(vecProposalsTemp.empty()) break;
        nHashProposal = vecProposalsTemp.back()->GetHash();

        bool fAsked = false;
        for (const auto& pnode : vNodesCopy) {
            // Only use regular peers, don't try to ask from outbound "masternode" connections -
            // they stay connected for a short period of time and it's possible that we won't get everything we should.
            // Only use outbound connections - inbound connection could be a "masternode" connection
            // initiated from another node, so skip it too.
            if(pnode->fSmartnode || (fSmartNode && pnode->fInbound)) continue;
            // only use up to date peers
            if(pnode->nVersion < MIN_VOTING_PEER_PROTO_VERSION) continue;
            // stop early to prevent setAskFor overflow
            size_t nProjectedSize = pnode->setAskFor.size() + nProjectedVotes;
            if(nProjectedSize > SETASKFOR_MAX_SZ/2) continue;
            // to early to ask the same node
            if(mapAskedRecently[nHashProposal].count(pnode->addr)) continue;

            RequestProposal(pnode, nHashProposal, connman, true);
            mapAskedRecently[nHashProposal][pnode->addr] = nNow + nTimeout;
            fAsked = true;
            // stop loop if max number of peers per obj was asked
            if(mapAskedRecently[nHashProposal].size() >= nPeersPerHashMax) break;
        }
        // NOTE: this should match `if` above (the one before `while`)

        vecProposalsTemp.pop_back();

        if(!fAsked) i--;
    }
    LogPrint("proposal", "CSmartVotingManager::RequestProposalVotes -- end: vecProposalsTemp %d mapAskedRecently %d\n",
                vecProposalsTemp.size(), mapAskedRecently.size());

    return static_cast<int>(vecProposalsTemp.size());
}

bool CSmartVotingManager::AcceptProposalMessage(const uint256& nHash)
{
    LOCK(cs);
    return AcceptMessage(nHash, setRequestedProposals);
}

bool CSmartVotingManager::AcceptVoteMessage(const uint256& nHash)
{
    LOCK(cs);
    return AcceptMessage(nHash, setRequestedVotes);
}

bool CSmartVotingManager::AcceptMessage(const uint256& nHash, hash_s_t& setHash)
{
    hash_s_it it = setHash.find(nHash);
    if(it == setHash.end()) {
        // We never requested this
        return false;
    }
    // Only accept one response
    setHash.erase(it);
    return true;
}

void CSmartVotingManager::RebuildIndexes()
{
    LOCK(cs);

    cmapVoteToProposal.Clear();
    for(proposal_m_it it = mapProposals.begin(); it != mapProposals.end(); ++it) {
        CProposal& proposal = it->second;
        std::vector<CProposalVote> vecVotes = proposal.GetVoteFile().GetVotes();
        for(size_t i = 0; i < vecVotes.size(); ++i) {
            cmapVoteToProposal.Insert(vecVotes[i].GetHash(), &proposal);
        }
    }
}

void CSmartVotingManager::InitOnLoad()
{
    LOCK(cs);
    int64_t nStart = GetTimeMillis();
    LogPrintf("Preparing votingkey indexes...\n");
    RebuildIndexes();
    LogPrintf("Votingkey indexes prepared  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("     %s\n", ToString());
}

std::string CSmartVotingManager::ToString() const
{
    LOCK(cs);

    return strprintf("Proposals: %d,  Erased: %d, Votes: %d",
                    (int)mapProposals.size(),
                    (int)mapErasedProposals.size(),
                    (int)cmapVoteToProposal.GetSize());
}

UniValue CSmartVotingManager::ToJson() const
{
    LOCK(cs);

    UniValue jsonObj(UniValue::VOBJ);
    jsonObj.push_back(Pair("proposals", (int)mapProposals.size()));
    jsonObj.push_back(Pair("erased", (int)mapErasedProposals.size()));
    jsonObj.push_back(Pair("votes", (int)cmapVoteToProposal.GetSize()));
    return jsonObj;
}

void CSmartVotingManager::UpdatedBlockTip(const CBlockIndex *pindex, CConnman& connman)
{
    // Note this gets called from ActivateBestChain without cs_main being held
    // so it should be safe to lock our mutex here without risking a deadlock
    // On the other hand it should be safe for us to access pindex without holding a lock
    // on cs_main because the CBlockIndex objects are dynamically allocated and
    // presumably never deleted.
    if(!pindex) {
        return;
    }

    nCachedBlockHeight = pindex->nHeight;
    LogPrint("proposal", "CSmartVotingManager::UpdatedBlockTip -- nCachedBlockHeight: %d\n", nCachedBlockHeight);

    CheckPostponedProposals(connman);
}

void CSmartVotingManager::RequestOrphanProposals(CConnman& connman)
{
    std::vector<CNode*> vNodesCopy = connman.CopyNodeVector(CConnman::FullyConnectedOnly);

    std::vector<uint256> vecHashesFiltered;
    {
        std::vector<uint256> vecHashes;
        LOCK(cs);
        cmmapOrphanVotes.GetKeys(vecHashes);
        for(size_t i = 0; i < vecHashes.size(); ++i) {
            const uint256& nHash = vecHashes[i];
            if(mapProposals.find(nHash) == mapProposals.end()) {
                vecHashesFiltered.push_back(nHash);
            }
        }
    }

    LogPrint("proposal", "CProposal::RequestOrphanProposals -- number objects = %d\n", vecHashesFiltered.size());
    for(size_t i = 0; i < vecHashesFiltered.size(); ++i) {
        const uint256& nHash = vecHashesFiltered[i];
        for(size_t j = 0; j < vNodesCopy.size(); ++j) {
            CNode* pnode = vNodesCopy[j];
            if(pnode->fSmartnode) {
                continue;
            }
            RequestProposal(pnode, nHash, connman);
        }
    }

    connman.ReleaseNodeVector(vNodesCopy);
}

void CSmartVotingManager::CleanOrphanObjects()
{
    LOCK(cs);
    const vote_cmm_t::list_t& items = cmmapOrphanVotes.GetItemList();

    int64_t nNow = GetAdjustedTime();

    vote_cmm_t::list_cit it = items.begin();
    while(it != items.end()) {
        vote_cmm_t::list_cit prevIt = it;
        ++it;
        const vote_time_pair_t& pairVote = prevIt->value;
        if(pairVote.second < nNow) {
            cmmapOrphanVotes.Erase(prevIt->key, prevIt->value);
        }
    }
}

