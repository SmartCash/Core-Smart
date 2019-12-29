// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartvoting/manager.h"
#include "smartvoting/proposal.h"
#include "smartvoting/votevalidation.h"
#include "smartnode/smartnodesync.h"
#include "smartnode/instantx.h"
#include "validation.h"

const size_t nProposalTitleLengthMin = 10;
const size_t nProposalTitleLengthMax = 200;

const int64_t nProposalMilestoneDistanceMax = 60; // 60 days
const size_t nProposalMilestoneDescriptionLengthMin = 10;
const size_t nProposalMilestoneDescriptionLengthMax = 100;

static bool char_isspace(char c) {
    return std::isspace(static_cast<unsigned char>(c));
}


CProposal::CProposal() :
    title(),
    url(),
    address(),
    vecMilestones(),
    nTimeCreated(0),
    nTimeDeletion(0),
    nFeeHash(),
    fCachedLocalValidity(false),
    fCachedFunding(false),
    fCachedValid(true),
    fDirtyCache(true),
    fExpired(false),
    nCreationHeight(-1),
    mapCurrentVKVotes(),
    cmmapOrphanVotes(),
    fileVotes(),
    cs()
{

}

CProposal::CProposal(const CProposal &other) :
    title(other.title),
    url(other.url),
    address(other.address),
    vecMilestones(other.vecMilestones),
    nTimeCreated(other.nTimeCreated),
    nTimeDeletion(other.nTimeDeletion),
    nFeeHash(other.nFeeHash),
    fCachedLocalValidity(other.fCachedLocalValidity),
    fCachedFunding(other.fCachedFunding),
    fCachedValid(other.fCachedValid),
    fDirtyCache(other.fDirtyCache),
    fExpired(other.fExpired),
    nCreationHeight(other.nCreationHeight),
    mapCurrentVKVotes(other.mapCurrentVKVotes),
    cmmapOrphanVotes(other.cmmapOrphanVotes),
    fileVotes(other.fileVotes),
    cs()
{}

void CProposal::swap(CProposal &first, CProposal &second)
{
    // enable ADL (not necessary in our case, but good practice)
    using std::swap;

    // by swapping the members of two classes,
    // the two classes are effectively swapped
    swap(first.title, second.title);
    swap(first.url, second.url);
    swap(first.address, second.address);
    swap(first.vecMilestones, second.vecMilestones);
    swap(first.nTimeCreated, second.nTimeCreated);
    swap(first.nTimeDeletion, second.nTimeDeletion);
    swap(first.nFeeHash, second.nFeeHash);

    // swap all cached flags
    swap(first.fCachedLocalValidity, second.fCachedLocalValidity);
    swap(first.fCachedFunding, second.fCachedFunding);
    swap(first.fCachedValid, second.fCachedValid);
    swap(first.fDirtyCache, second.fDirtyCache);
    swap(first.fExpired, second.fExpired);
    swap(first.nCreationHeight, second.nCreationHeight);
}


bool CProposal::ValidateTitle(const string& strTitle, string& strError)
{
    strError.clear();

    std::string strClean = strTitle;

    strClean.erase(std::remove_if(strClean.begin(), strClean.end(), char_isspace), strClean.end());

    if( strClean.length() < nProposalTitleLengthMin)
        strError = strprintf("Title too short. Minimum required: %d characters (whitespaces excluded).",nProposalTitleLengthMin);
    else if( strClean.length() > nProposalTitleLengthMax )
        strError = strprintf("Title too long. Maximum allowed: %d characters (whitespaces excluded).",nProposalTitleLengthMax);

    return strError.empty();
}

/*
  The purpose of this function is to replicate the behavior of the
  Python urlparse function used by sentinel (urlparse.py).  This function
  should return false whenever urlparse raises an exception and true
  otherwise.
 */
bool CProposal::CheckURL(const std::string& strURLIn)
{
    std::string strRest(strURLIn);
    std::string::size_type nPos = strRest.find(':');

    if(nPos != std::string::npos) {
        //std::string strSchema = strRest.substr(0,nPos);

        if(nPos < strRest.size()) {
            strRest = strRest.substr(nPos + 1);
        }
        else {
            strRest = "";
        }
    }

    // Process netloc
    if((strRest.size() > 2) && (strRest.substr(0,2) == "//")) {
        static const std::string strNetlocDelimiters = "/?#";

        strRest = strRest.substr(2);

        std::string::size_type nPos2 = strRest.find_first_of(strNetlocDelimiters);

        std::string strNetloc = strRest.substr(0,nPos2);

        if((strNetloc.find('[') != std::string::npos) && (strNetloc.find(']') == std::string::npos)) {
            return false;
        }

        if((strNetloc.find(']') != std::string::npos) && (strNetloc.find('[') == std::string::npos)) {
            return false;
        }
    }

    return true;
}

bool CProposal::ValidateUrl(const string& strUrl, string& strError)
{
    strError.clear();

    if(std::find_if(strUrl.begin(), strUrl.end(), ::isspace) != strUrl.end()) {
        strError += "URL can't have whitespaces";
        return false;
    }

    if(strUrl.size() < 8U) {
        strError += "URL too short, minimum length is 8 characters";
        return false;
    }

    if(strUrl.size() > 200U) {
        strError += "URL too long, maximum length is 200 characters";
        return false;
    }

    if(!CProposal::CheckURL(strUrl)) {
        strError += "URL format invalid";
        return false;
    }

    return true;
}

bool CProposal::IsTitleValid(string& strError) const
{
    return ValidateTitle(title, strError);
}

bool CProposal::IsUrlValid(string& strError) const
{
    return ValidateUrl(url, strError);
}

bool CProposal::IsAddressValid(string& strError) const
{
    strError.clear();

    if( !address.IsValid() ){
        strError = "Invalid SmartCash address";
        return false;
    }

    return true;
}

bool CProposal::IsMilestoneVectorValid(std::string& strError) const
{
    strError.clear();

    if( !vecMilestones.size() )
        strError = "At least 1 milestone required";
    else{
        CProposalMilestone distanceCheck;

        for( size_t i = 0; i<vecMilestones.size();i++){
            std::string strErrTmp;

            if( i == 0 ){
                distanceCheck = vecMilestones.at(i);
            }else{

                int64_t distance = vecMilestones.at(i).GetTime() - distanceCheck.GetTime();
                if( distance > nProposalMilestoneDistanceMax * 24 * 60 * 60 ){
                    strError += strprintf("Milestone #%d: Maximum milestone length is %d days\n", i, nProposalMilestoneDistanceMax);
                }

                distanceCheck = vecMilestones.at(i);
            }

            if( !vecMilestones.at(i).IsDescriptionValid(strErrTmp) ){
                strError += strprintf("Milestone #%d: %s\n", i, strErrTmp);
            }

        }
    }

    return strError.empty();
}

int CProposal::GetRequestedAmount() const
{
    int nAmount = 0;
    for( const CProposalMilestone& milestone : vecMilestones ){
        nAmount += milestone.GetAmount();
    }
    return nAmount;
}

bool CProposal::IsValid(std::vector<std::string> &vecErrors) const
{
    vecErrors.clear();

    std::string strError;
    if( !IsTitleValid(strError) ) vecErrors.push_back(strError);
    if( !IsUrlValid(strError) ) vecErrors.push_back(strError);
    if( !IsAddressValid(strError) ) vecErrors.push_back(strError);
    if( !IsMilestoneVectorValid(strError) ) vecErrors.push_back(strError);

    return !vecErrors.size();
}

bool CProposal::IsValid() const
{
    std::vector<std::string> vecErrors;
    return IsValid(vecErrors);
}

uint256 CProposal::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);

    ss << nTimeCreated;
    ss << title;
    ss << url;
    ss << address;
    ss << vecMilestones;

    return ss.GetHash();
}

CInternalProposal::CInternalProposal(const CInternalProposal &other) :
    CProposal(other),
    hashInternal(other.hashInternal),
    fPaid(other.fPaid),
    fPublished(other.fPublished),
    rawFeeTx(other.rawFeeTx),
    strSignedHash(other.strSignedHash)
{

}

void CInternalProposal::AddMilestone(CProposalMilestone &milestone)
{
    vecMilestones.push_back(milestone);
    std::sort(vecMilestones.begin(), vecMilestones.end());
}

void CInternalProposal::RemoveMilestone(size_t index)
{
    if( index < vecMilestones.size() )
        vecMilestones.erase(vecMilestones.begin() + index);

    std::sort(vecMilestones.begin(), vecMilestones.end());
}

bool CProposalMilestone::IsDescriptionValid(string &strError) const
{
    strError.clear();

    std::string strClean = strDescription;

    strClean.erase(std::remove_if(strClean.begin(), strClean.end(), char_isspace), strClean.end());

    if( strClean.length() < nProposalMilestoneDescriptionLengthMin)
        strError = strprintf("Description too short. Minimum required: %d characters (whitespaces excluded).",nProposalMilestoneDescriptionLengthMin);
    else if( strClean.length() > nProposalMilestoneDescriptionLengthMax )
        strError = strprintf("Description too long. Maximum allowed: %d characters (whitespaces excluded).",nProposalMilestoneDescriptionLengthMax);

    return strError.empty();
}



bool CProposal::ProcessVote(CNode* pfrom,
                            const CProposalVote& vote,
                            CSmartVotingException& exception,
                            CConnman& connman)
{
    LOCK(cs);

    // do not process already known valid votes twice
    if (fileVotes.HasVote(vote.GetHash())) {
        // nothing to do here, not an error
        std::ostringstream ostr;
        ostr << "Already known valid vote";
        LogPrint("proposal", "CProposal::ProcessVote -- %s\n", ostr.str());
        exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_NONE);
        return false;
    }

    vote_m_it it = mapCurrentVKVotes.emplace(vote_m_t::value_type(vote.GetVoteKey(), vote_rec_t())).first;
    vote_rec_t& voteRecordRef = it->second;
    vote_signal_enum_t eSignal = vote.GetSignal();
    if(eSignal == VOTE_SIGNAL_NONE) {
        std::ostringstream ostr;
        ostr << "Vote signal: none";
        LogPrint("proposal", "CProposal::ProcessVote -- %s\n", ostr.str());
        exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_WARNING);
        return false;
    }
    if(eSignal > MAX_SUPPORTED_VOTE_SIGNAL) {
        std::ostringstream ostr;
        ostr << "Unsupported vote signal: " << CProposalVoting::ConvertSignalToString(vote.GetSignal());
        LogPrint("proposal", "CProposal::ProcessVote -- %s\n", ostr.str());
        exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_PERMANENT_ERROR, 20);
        return false;
    }
    vote_instance_m_it it2 = voteRecordRef.mapInstances.emplace(vote_instance_m_t::value_type(int(eSignal), vote_instance_t())).first;
    vote_instance_t& voteInstanceRef = it2->second;

    // Reject obsolete votes
    if(vote.GetTimestamp() < voteInstanceRef.nCreationTime) {
        std::ostringstream ostr;
        ostr << "Obsolete vote" << vote.ToString();
        ostr << ", newer vote time " << voteInstanceRef.nCreationTime;
        LogPrint("proposal", "CProposal::ProcessVote -- %s\n", ostr.str());
        exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_WARNING);
        return false;
    }

// TODO VOTE RATE LIMIT CHECKS
//    int64_t nNow = GetAdjustedTime();
    int64_t nVoteTimeUpdate = voteInstanceRef.nTime;
//    if(governance.AreRateChecksEnabled()) {
//        int64_t nTimeDelta = nNow - voteInstanceRef.nTime;
//        if(nTimeDelta < SMARTVOTING_UPDATE_MIN) {
//            std::ostringstream ostr;
//            ostr << "CProposal::ProcessVote -- Masternode voting too often"
//                 << ", MN outpoint = " << vote.GetMasternodeOutpoint().ToStringShort()
//                 << ", governance object hash = " << GetHash().ToString()
//                 << ", time delta = " << nTimeDelta;
//            LogPrint("proposal", "%s\n", ostr.str());
//            exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_TEMPORARY_ERROR);
//            nVoteTimeUpdate = nNow;
//            return false;
//        }
//    }

    // Finally check that the vote is actually valid (done last because of cost of signature verification)
    std::string strError;
    if(!vote.IsValid(true, true, strError)) {
        std::ostringstream ostr;
        ostr << "Invalid vote "
             << ", error = " << strError
             << ", proposal hash = " << GetHash().ToString()
             << ", vote hash = " << vote.GetHash().ToString();
        LogPrint("proposal", "CProposal::ProcessVote -- %s\n", ostr.str());
        exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_PERMANENT_ERROR, 20);
        smartVoting.AddInvalidVote(vote);
        return false;
    }

// TODO maybe, add votes to the walletdb
//    if(!mnodeman.AddGovernanceVote(vote.GetMasternodeOutpoint(), vote.GetParentHash())) {
//        std::ostringstream ostr;
//        ostr << "CProposal::ProcessVote -- Unable to add governance vote"
//             << ", MN outpoint = " << vote.GetMasternodeOutpoint().ToStringShort()
//             << ", governance object hash = " << GetHash().ToString();
//        LogPrint("proposal", "%s\n", ostr.str());
//        exception = CSmartVotingException(ostr.str(), SMARTVOTING_EXCEPTION_PERMANENT_ERROR);
//        return false;
//    }

    voteInstanceRef = vote_instance_t(vote.GetOutcome(), nVoteTimeUpdate, vote.GetTimestamp());
    fileVotes.AddVote(vote);
    InvalidateVoteCache();
    return true;
}

void CProposal::ClearVoteKeyVotes()
{
    LOCK(cs);

    vote_m_it it = mapCurrentVKVotes.begin();
    while(it != mapCurrentVKVotes.end()) {
//        if(!mnodeman.Has(it->first)) {
            fileVotes.RemoveVotesFromVotingKey(it->first);
            mapCurrentVKVotes.erase(it++);
//        }
//        else {
//            ++it;
//        }
    }
}

void CProposal::UpdateLocalValidity()
{
    LOCK(cs_main);
    // THIS DOES NOT CHECK COLLATERAL, THIS IS CHECKED UPON ORIGINAL ARRIVAL
    fCachedLocalValidity = IsValidLocally(strLocalValidityError, false);
}

bool CProposal::IsValidLocally(std::string& strError, bool fCheckCollateral) const
{
    int fMissingConfirmations = -1;

    return IsValidLocally(strError, fMissingConfirmations, fCheckCollateral);
}

bool CProposal::IsValidLocally(std::string& strError, int& fMissingConfirmations, bool fCheckCollateral) const
{
    fMissingConfirmations = -1;

    // Note: It's ok to have expired proposals
    // they are going to be cleared by CSmartVotingManager::UpdateCachesAndClean()
    // TODO: should they be tagged as "expired" to skip vote downloading?
    std::vector<std::string> vecErrors;
    if (!IsValid(vecErrors)) {
        strError = strprintf("Invalid proposal data, error messages: %s", vecErrors.front());
        return false;
    }
    if (fCheckCollateral && !IsCollateralValid(strError, fMissingConfirmations) ) {
        return false;
    }
    return true;
}

bool CProposal::UpdateProposalStartHeight(){

    std::string strError;

    // If we already set the height successully
    if( nCreationHeight != -1 ) return true;

    CTransaction txCollateral;
    uint256 nBlockHash;
    if(!GetTransaction(nFeeHash, txCollateral, Params().GetConsensus(), nBlockHash, true)){
        strError = strprintf("Can't find fee tx %s", nFeeHash.ToString());
        LogPrintf("CProposal::IsCollateralValid -- %s\n", strError);
        return false;
    }

    AssertLockHeld(cs_main);

    if (nBlockHash != uint256()) {
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                nCreationHeight = pindex->nHeight;
                return true;
            }
        }
    }
    return false;
}

bool CProposal::IsCollateralValid(std::string& strError, int& fMissingConfirmations) const
{
    strError = "";
    fMissingConfirmations = -1;
    CAmount nMinFee = SMARTVOTING_PROPOSAL_FEE;
    uint256 nExpectedHash = GetHash();

    CTransaction txCollateral;
    uint256 nBlockHash;

    // RETRIEVE TRANSACTION IN QUESTION

    if(!GetTransaction(nFeeHash, txCollateral, Params().GetConsensus(), nBlockHash, true)){
        strError = strprintf("Can't find fee tx %s", nFeeHash.ToString());
        LogPrintf("CProposal::IsCollateralValid -- %s\n", strError);
        return false;
    }

    if(nBlockHash == uint256()) {
        strError = strprintf("Fee tx %s is not mined yet", txCollateral.ToString());
        LogPrintf("CProposal::IsCollateralValid -- %s\n", strError);
        return false;
    }

    if(txCollateral.vout.size() < 2) {
        strError = strprintf("tx vout size less than 2 | %d", txCollateral.vout.size());
        LogPrintf("CProposal::IsCollateralValid -- %s\n", strError);
        return false;
    }

    // LOOK FOR SPECIALIZED PROPOSAL SCRIPTS

    CScript findDataScript;
    findDataScript << OP_RETURN << ToByteVector(nExpectedHash);

//    CScript findHiveScript = SmartHive::Script(SmartHive::ProjectTreasury);

    bool fFoundOpReturn = false;
    bool fFoundFee = false;
    for (const auto& output : txCollateral.vout) {
        DBG( std::cout << "IsCollateralValid txout : " << output.ToString()
             << ", output.nValue = " << output.nValue
             << ", output.scriptPubKey = " << ScriptToAsmStr( output.scriptPubKey, false )
             << std::endl; );
        if(!output.scriptPubKey.IsPayToPublicKeyHash() && !output.scriptPubKey.IsUnspendable()) {
            strError = strprintf("Invalid Script %s", txCollateral.ToString());
            LogPrintf ("CProposal::IsCollateralValid -- %s\n", strError);
            return false;
        }
//        if(output.scriptPubKey == findHiveScript && output.nValue >= nMinFee) {
//            DBG( std::cout << "IsCollateralValid fFoundFee = true" << std::endl; );
//            fFoundFee = true;
//        }
        if(output.scriptPubKey == findDataScript && output.nValue == 0) {
            DBG( std::cout << "IsCollateralValid fFoundOpReturn = true" << std::endl; );
            fFoundOpReturn = true;
        }
        else  {
            DBG( std::cout << "IsCollateralValid No match, continuing" << std::endl; );
        }

    }

    if(!fFoundOpReturn){
        strError = strprintf("Couldn't find opReturn %s in %s", nExpectedHash.ToString(), txCollateral.ToString());
        LogPrintf ("CProposal::IsCollateralValid -- %s\n", strError);
        return false;
    }

    if(!fFoundFee){
        strError = strprintf("Couldn't find proposal fee output %s in %s", nExpectedHash.ToString(), txCollateral.ToString());
        LogPrintf ("CProposal::IsCollateralValid -- %s\n", strError);
        return false;
    }

    // GET CONFIRMATIONS FOR TRANSACTION

    AssertLockHeld(cs_main);
    int nConfirmationsIn = instantsend.GetConfirmations(nFeeHash);
    if (nBlockHash != uint256()) {
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                nConfirmationsIn += chainActive.Height() - pindex->nHeight + 1;
            }
        }
    }

    if(nConfirmationsIn < SMARTVOTING_FEE_CONFIRMATIONS) {
        strError = strprintf("Collateral requires at least %d confirmations to be relayed throughout the network (it has only %d)", SMARTVOTING_FEE_CONFIRMATIONS, nConfirmationsIn);
        if (nConfirmationsIn >= SMARTVOTING_MIN_RELAY_FEE_CONFIRMATIONS) {
            strError += ", pre-accepted -- waiting for required confirmations";
        } else {
            strError += ", rejected -- try again later";
        }
        LogPrintf ("CProposal::IsCollateralValid -- %s\n", strError);

        fMissingConfirmations = std::max<int>(0,SMARTVOTING_FEE_CONFIRMATIONS - nConfirmationsIn);

        return fMissingConfirmations <= (SMARTVOTING_FEE_CONFIRMATIONS-SMARTVOTING_MIN_RELAY_FEE_CONFIRMATIONS);
    }

    strError = "valid";
    return true;
}

int64_t CProposal::GetVotingPower(vote_signal_enum_t eVoteSignalIn, vote_outcome_enum_t eVoteOutcomeIn) const
{
    LOCK(cs);

    int64_t nTotalPower = 0;
    for (const auto& votepair : mapCurrentVKVotes) {
        const vote_rec_t& recVote = votepair.second;
        vote_instance_m_cit it2 = recVote.mapInstances.find(eVoteSignalIn);
        if(it2 != recVote.mapInstances.end() && it2->second.eOutcome == eVoteOutcomeIn) {
            int64_t nPower = ::GetVotingPower(votepair.first);
            nTotalPower += std::max<int64_t>(0,nPower);
        }
    }
    return nTotalPower;
}

CVoteOutcomes CProposal::GetVotingPower(const std::set<CVoteKey> &setVoteKeys, vote_signal_enum_t eVoteSignalIn) const
{
    LOCK(cs);

    CVoteOutcomes outcome;

    for (const auto& vk : setVoteKeys) {

        int64_t nPower = ::GetVotingPower(vk);
        // Its -1 if the votekey got not updated yet
        nPower = std::max<int64_t>(0,nPower);

        vote_rec_t recVotes;
        if( GetCurrentVKVotes(vk, recVotes) ){
            vote_instance_m_cit it = recVotes.mapInstances.find(eVoteSignalIn);
            if( it == recVotes.mapInstances.end()  ) continue;

            switch(it->second.eOutcome){
            case VOTE_OUTCOME_YES:
                outcome.nYesPower += nPower;
                break;
            case VOTE_OUTCOME_NO:
                outcome.nNoPower += nPower;
                break;
            case VOTE_OUTCOME_ABSTAIN:
                outcome.nAbstainPower += nPower;
                break;
            case VOTE_OUTCOME_NONE:
                break;

            }

        }
    }
    return outcome;
}

/**
*   Get specific vote counts for each outcome (funding, validity, etc)
*/

CAmount CProposal::GetAbsoluteYesPower(vote_signal_enum_t eVoteSignalIn) const
{
    return GetYesPower(eVoteSignalIn) - GetNoPower(eVoteSignalIn);
}

CAmount CProposal::GetAbsoluteNoPower(vote_signal_enum_t eVoteSignalIn) const
{
    return GetNoPower(eVoteSignalIn) - GetYesPower(eVoteSignalIn);
}

CAmount CProposal::GetYesPower(vote_signal_enum_t eVoteSignalIn) const
{
    return GetVotingPower(eVoteSignalIn, VOTE_OUTCOME_YES);
}

CAmount CProposal::GetNoPower(vote_signal_enum_t eVoteSignalIn) const
{
    return GetVotingPower(eVoteSignalIn, VOTE_OUTCOME_NO);
}

CAmount CProposal::GetAbstainPower(vote_signal_enum_t eVoteSignalIn) const
{
    return GetVotingPower(eVoteSignalIn, VOTE_OUTCOME_ABSTAIN);
}

CVoteResult CProposal::GetVotingResult(vote_signal_enum_t eVoteSignalIn) const
{
    return CVoteResult(GetYesPower(eVoteSignalIn),
                       GetNoPower(eVoteSignalIn),
                       GetAbstainPower(eVoteSignalIn));
}

void CProposal::GetActiveVoteKeys(std::set<CVoteKey> &setVoteKeys) const
{
    for( auto it : mapCurrentVKVotes ){
        if( !setVoteKeys.count(it.first)){
            setVoteKeys.insert(it.first);
        }
    }
}

bool CProposal::GetCurrentVKVotes(const CVoteKey &voteKey, vote_rec_t& voteRecord) const
{
    LOCK(cs);

    vote_m_cit it = mapCurrentVKVotes.find(voteKey);
    if (it == mapCurrentVKVotes.end()) {
        return false;
    }
    voteRecord = it->second;
    return  true;
}

int CProposal::GetValidVoteEndHeight() const
{
    if( GetVotingStartHeight() > 0 )
        return GetVotingStartHeight() + Params().GetConsensus().nProposalValidityVoteBlocks;

    return 0;
}

int CProposal::GetFundingVoteEndHeight() const
{
    if( GetVotingStartHeight() > 0 )
        return GetVotingStartHeight() + Params().GetConsensus().nProposalFundingVoteBlocks;

    return 0;
}

void CProposal::Relay(CConnman& connman) const
{
    // Do not relay until fully synced
    if(!smartnodeSync.IsSynced()) {
        LogPrint("proposal", "CProposal::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_VOTING_PROPOSAL, GetHash());
    connman.RelayInv(inv, MIN_VOTING_PEER_PROTO_VERSION);
}

void CProposal::CheckOrphanVotes(CConnman& connman)
{
    int64_t nNow = GetAdjustedTime();
    const vote_cmm_t::list_t& listVotes = cmmapOrphanVotes.GetItemList();
    vote_cmm_t::list_cit it = listVotes.begin();
    while(it != listVotes.end()) {
        bool fRemove = false;
        const COutPoint& key = it->key;
        const vote_time_pair_t& pairVote = it->value;
        const CProposalVote& vote = pairVote.first;
        if(pairVote.second < nNow) {
            fRemove = true;
        }
// TODO check for valid voting key
//        else if(!mnodeman.Has(vote.GetVoteKey())) {
//            ++it;
//            continue;
//        }
        CSmartVotingException exception;
        if(!ProcessVote(NULL, vote, exception, connman)) {
            LogPrintf("CProposal::CheckOrphanVotes -- Failed to add orphan vote: %s\n", exception.what());
        }
        else {
            vote.Relay(connman);
            fRemove = true;
        }
        ++it;
        if(fRemove) {
            cmmapOrphanVotes.Erase(key, pairVote);
        }
    }
}


void CProposal::UpdateSentinelVariables()
{
    Consensus::Params consensus = Params().GetConsensus();

    int64_t nCurrentHeight = chainActive.Height();


    // SET SENTINEL FLAGS TO FALSE

    fCachedFunding = false;
    fCachedValid = true; //default to valid
    fDirtyCache = false;

    // SET SENTINEL FLAGS TO TRUE IF MIMIMUM SUPPORT LEVELS ARE REACHED
    // ARE ANY OF THESE FLAGS CURRENTLY ACTIVATED?

    CVoteResult fundingResult = GetVotingResult(VOTE_SIGNAL_FUNDING);
    CVoteResult validResult = GetVotingResult(VOTE_SIGNAL_VALID);

    if( UpdateProposalStartHeight() ){

        int nValidEndHeight = GetValidVoteEndHeight();
        //int nFundingEndHeight = GetFundingVoteEndHeight();

        if( fundingResult.percentYes > consensus.nVotingMinYesPercent ) fCachedFunding = true;

        if( nValidEndHeight && nCurrentHeight > nValidEndHeight &&
            validResult.GetTotalPower() &&
            validResult.percentYes < consensus.nVotingMinYesPercent) {

            fCachedValid = false;
            if(nTimeDeletion == 0) {
                nTimeDeletion = GetAdjustedTime();
            }
        }
    }
}

string CProposal::ToString() const
{
    return strprintf("CProposal(%s, %s, %s, %s)", GetHash().ToString(), title, url, address.ToString());
}

string CInternalProposal::ToString() const
{
    return strprintf("CInternalProposal %s -- %s", hashInternal.ToString(), CProposal::ToString());
}
