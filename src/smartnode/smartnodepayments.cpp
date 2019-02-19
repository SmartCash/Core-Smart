// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activesmartnode.h"
#include "base58.h"
#include "smartnodepayments.h"
#include "smartnodesync.h"
#include "smartnodeman.h"
#include "smarthive/hive.h"
#include "../messagesigner.h"
#include "netfulfilledman.h"
#include "script/standard.h"
#include "spork.h"
#include "../util.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"

#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CSmartnodePayments mnpayments;

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapSmartnodeBlocks;
CCriticalSection cs_mapSmartnodePaymentVotes;

struct CompareBlockPayees
{
    bool operator()(const CSmartnodePayee& t1,
                    const CSmartnodePayee& t2) const
    {
        return t1.GetVoteCount() != t2.GetVoteCount() ? t1.GetVoteCount() > t2.GetVoteCount() : t1.GetVoteHashes().front() < t2.GetVoteHashes().front();
    }
};

int SmartNodePayments::PayoutsPerBlock(int nHeight)
{
    if( MainNet() ){

        if(nHeight >= HF_V1_2_MULTINODE_VOTING_HEIGHT && nHeight < HF_V1_2_MULTINODE_PAYOUT_HEIGHT){
            return 1;
        }else if(nHeight >= HF_V1_2_MULTINODE_PAYOUT_HEIGHT && nHeight < HF_V1_2_8_SMARNODE_NEW_COLLATERAL_HEIGHT){
            return HF_V1_2_NODES_PER_BLOCK;
        }else if(nHeight >= HF_V1_2_8_SMARNODE_NEW_COLLATERAL_HEIGHT){
            return HF_V1_2_8_NODES_PER_BLOCK;
        }

    }else{

        if(nHeight >= TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_1 && nHeight < TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_2)
            return TESTNET_V1_2_NODES_PER_BLOCK_1;
        if(nHeight >= TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_2 && nHeight < TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_3)
            return TESTNET_V1_2_NODES_PER_BLOCK_2;
        if(nHeight >= TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_3 && nHeight < TESTNET_V1_2_8_SMARNODE_NEW_COLLATERAL_HEIGHT)
            return TESTNET_V1_2_NODES_PER_BLOCK_3;
        if(nHeight >= TESTNET_V1_2_8_SMARNODE_NEW_COLLATERAL_HEIGHT)
            return TESTNET_V1_2_8_NODES_PER_BLOCK;

    }

    return 0;
}

int SmartNodePayments::PayoutInterval(int nHeight)
{
    if( MainNet() ){

        if(nHeight >= HF_V1_2_MULTINODE_VOTING_HEIGHT && nHeight < HF_V1_2_MULTINODE_PAYOUT_HEIGHT){
            return 1;
        }else if(nHeight >= HF_V1_2_MULTINODE_PAYOUT_HEIGHT){
            return HF_V1_2_NODES_BLOCK_INTERVAL;
        }

    }else{
        if(nHeight >= TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_1 && nHeight < TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_2)
            return TESTNET_V1_2_NODES_BLOCK_INTERVAL_1;
        if(nHeight >= TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_2 && nHeight < TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_3)
            return TESTNET_V1_2_NODES_BLOCK_INTERVAL_2;
        if(nHeight >= TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_3)
            return TESTNET_V1_2_NODES_BLOCK_INTERVAL_3;

    }

    return 0;
}

CAmount SmartNodePayments::Payment(int nHeight)
{
    CAmount blockValue = 0;

    if( MainNet() ){

        if( nHeight < HF_V1_1_SMARTNODE_HEIGHT ){
            blockValue = 0;
        }else if( nHeight >= HF_V1_1_SMARTNODE_HEIGHT &&
            nHeight < HF_V1_2_MULTINODE_PAYOUT_HEIGHT ){
            blockValue = GetBlockValue(nHeight,0,INT_MAX);
        }else if(nHeight >= HF_V1_2_MULTINODE_PAYOUT_HEIGHT){

            int interval = SmartNodePayments::PayoutInterval(nHeight);

            if( nHeight % interval ){
                blockValue = 0;
            }else{
                while( interval-- ) blockValue += GetBlockValue(nHeight--,0,INT_MAX);
            }

        }

    }else{

        if( nHeight < TESTNET_V1_2_PAYMENTS_HEIGHT ){
            blockValue = 0;
        }else if( nHeight >= TESTNET_V1_2_PAYMENTS_HEIGHT &&
            nHeight < TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_1 ){
            blockValue = GetBlockValue(nHeight,0,INT_MAX);
        }else if(nHeight >= TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_1){

            int interval = SmartNodePayments::PayoutInterval(nHeight);

            if( nHeight % interval ){
                blockValue = 0;
            }else{
                while( interval-- ) blockValue += GetBlockValue(nHeight--,0,INT_MAX);
            }

        }

    }

    return blockValue/10; // start at 10%
}

bool SmartNodePayments::IsPaymentValid(const CTransaction& txNew, int nHeight, CAmount blockReward, CAmount& nodeReward)
{
    nodeReward = SmartNodePayments::Payment(nHeight);

    if( MainNet() ){

        if( nHeight >= HF_V1_1_SMARTNODE_HEIGHT + 7000 && nHeight < HF_V1_2_MULTINODE_VOTING_HEIGHT ){

            BOOST_FOREACH(CTxOut txout, txNew.vout) {
                if (abs(txout.nValue - nodeReward) < 2) {
                    nodeReward = txout.nValue;
                    LogPrint("mnpayments", "CSmartnodeBlockPayees::IsTransactionValid -- Found required payment: %s\n",txout.ToString());
                    return true;
                }
            }

            if(sporkManager.IsSporkActive(SPORK_8_SMARTNODE_PAYMENT_ENFORCEMENT)) {
                LogPrint("mnpayments", "SmartNodePayments::IsPaymetValid -- ERROR: Invalid smartnode payment detected at height %d: %s", nHeight, txNew.ToString());
                return false;
            }

            return true;

        }else if( nHeight >= HF_V1_2_MULTINODE_VOTING_HEIGHT ){
            if( nHeight % SmartNodePayments::PayoutInterval(nHeight) ) return true;
        }

    }else{

        if( nHeight < TESTNET_V1_2_PAYMENTS_HEIGHT ){
            return true;
        }else if( nHeight >= TESTNET_V1_2_PAYMENTS_HEIGHT &&
            nHeight < TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_1 ){
            return true;
        }else if(nHeight >= TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_1){

            if( nHeight % SmartNodePayments::PayoutInterval(nHeight) ) return true;
        }

    }

    if(!smartnodeSync.IsSynced() || fLiteMode ) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        LogPrint("mnpayments", "SmartNodePayments::IsPaymetValid -- WARNING: Client not synced, skipping block payee checks\n");
        return true;
    }

    if(mnpayments.IsTransactionValid(txNew, nHeight, nodeReward)) {
        LogPrint("mnpayments", "SmartNodePayments::IsPaymetValid -- Valid smartnode payment at height %d: %s", nHeight, txNew.ToString());
        return true;
    }

    if(sporkManager.IsSporkActive(SPORK_8_SMARTNODE_PAYMENT_ENFORCEMENT)) {
        LogPrint("mnpayments", "SmartNodePayments::IsPaymetValid -- ERROR: Invalid smartnode payment detected at height %d: %s", nHeight, txNew.ToString());
        return false;
    }

    LogPrint("mnpayments", "IsPaymentValid -- WARNING: Smartnode payment enforcement is disabled, accepting any payee\n");

    return true;
}

void SmartNodePayments::FillPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartNodes)
{
    mnpayments.FillBlockPayee(txNew, nBlockHeight, blockReward, voutSmartNodes);
}

std::string SmartNodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    return mnpayments.GetRequiredPaymentsString(nBlockHeight);
}

UniValue SmartNodePayments::GetPaymentBlockObject(int nBlockHeight)
{
    return mnpayments.GetPaymentBlockObject(nBlockHeight);
}

void CSmartnodePayments::Clear()
{
    LOCK2(cs_mapSmartnodeBlocks, cs_mapSmartnodePaymentVotes);
    mapSmartnodeBlocks.clear();
    mapSmartnodePaymentVotes.clear();
}

bool CSmartnodePayments::UpdateLastVote(const CSmartnodePaymentVote& vote)
{
    LOCK(cs_mapSmartnodePaymentVotes);

    const auto it = mapSmartnodesLastVote.find(vote.vinSmartnode.prevout);
    if (it != mapSmartnodesLastVote.end()) {
        if (it->second == vote.nBlockHeight)
            return false;
        it->second = vote.nBlockHeight;
        return true;
    }

    //record this smartnode voted
    mapSmartnodesLastVote.emplace(vote.vinSmartnode.prevout, vote.nBlockHeight);
    return true;
}

/**
*   FillBlockPayee
*
*   Fill Smartnode ONLY payment block
*/

void CSmartnodePayments::FillBlockPayee(CMutableTransaction& txNew, int nHeight, CAmount blockReward, std::vector<CTxOut>& vxoutSmartNodes)
{
    vxoutSmartNodes.clear();

    if( MainNet() ){

        if( nHeight < HF_V1_1_SMARTNODE_HEIGHT ){
            return;
        }else if( nHeight >= HF_V1_1_SMARTNODE_HEIGHT &&
            nHeight < HF_V1_2_MULTINODE_VOTING_HEIGHT ){
            int nCount;
            CSmartNodeWinners mnInfos;
            if(!mnodeman.GetNextSmartnodesInQueueForPayment(nHeight, true, nCount, mnInfos)) {
                // ...and we can't calculate it on our own
                LogPrintf("CSmartnodePayments::FillBlockPayee -- Failed to detect smartnode to pay\n");
                return;
            }

            if( mnInfos.size() ){

                CScript payee = GetScriptForDestination(mnInfos.front().pubKeyCollateralAddress.GetID());

                CAmount smartnodePayment = SmartNodePayments::Payment(nHeight);

                CTxOut out = CTxOut(smartnodePayment, payee);
                vxoutSmartNodes.push_back(out);
                txNew.vout.push_back(out);
            }

            return;

        }else if(nHeight >= HF_V1_2_MULTINODE_VOTING_HEIGHT){
            if( nHeight % SmartNodePayments::PayoutInterval(nHeight) ) return;
            if( !SmartNodePayments::PayoutsPerBlock(nHeight) ) return;
        }

    }else{

        if( nHeight < TESTNET_V1_2_PAYMENTS_HEIGHT ){
            return;
        }else if( nHeight >= TESTNET_V1_2_PAYMENTS_HEIGHT &&
            nHeight < TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_1 ){
            return;
        }else if(nHeight >= TESTNET_V1_2_MULTINODE_PAYMENTS_HEIGHT_1){
            if( nHeight % SmartNodePayments::PayoutInterval(nHeight) ) return;
            if( !SmartNodePayments::PayoutsPerBlock(nHeight) ) return;
        }

    }

    CScriptVector payees;

    if(!mnpayments.GetBlockPayees(nHeight, payees)) {
        // no smartnode detected...
        int nCount = 0;
        CSmartNodeWinners mnInfos;
        if(!mnodeman.GetNextSmartnodesInQueueForPayment(nHeight, true, nCount, mnInfos)) {
            // ...and we can't calculate it on our own
            LogPrintf("CSmartnodePayments::FillBlockPayee -- Failed to detect smartnode to pay\n");
            return;
        }
        // fill payee with locally calculated winner and hope for the best
        for( const auto& mnInfo : mnInfos)
            payees.push_back(GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID()));
    }

    // GET SMARTNODE PAYMENT VARIABLES SETUP
    CAmount smartnodeBlockPayment = SmartNodePayments::Payment(nHeight);
    CAmount smartnodePayment = smartnodeBlockPayment / SmartNodePayments::PayoutsPerBlock(nHeight);

    for( const auto& payee : payees ){

        // ... and smartnode
        CTxOut out = CTxOut(smartnodePayment, payee);
        vxoutSmartNodes.push_back(out);
        txNew.vout.push_back(out);

        CTxDestination destination;
        ExtractDestination(payee, destination);
        CBitcoinAddress address(destination);

        LogPrintf("CSmartnodePayments::FillBlockPayee -- Smartnode payment %lld to %s\n", smartnodePayment, address.ToString());
    }
}

int CSmartnodePayments::GetMinSmartnodePaymentsProto() {

    int64_t nProtocolSpork = sporkManager.GetSporkValue(SPORK_21_SMARTNODE_PROTOCOL_REQUIREMENT);

    int nProtocolOld = PROTOCOL_BASE_VERSION + (nProtocolSpork & 0xFF);
    int nProtocolNew = PROTOCOL_BASE_VERSION + ((nProtocolSpork >> 8) & 0xFF);
    int nProtocolActiveTime =  nProtocolSpork >> 16;

    // If we crossed the threshold return the new protocol
    if( GetAdjustedTime() > nProtocolActiveTime ){
        return nProtocolNew;
    }

    // If not the old one..
    return nProtocolOld;
}

void CSmartnodePayments::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if(fLiteMode) return; // disable all SmartCash specific functionality

    if (strCommand == NetMsgType::SMARTNODEPAYMENTSYNC) { //Smartnode Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after smartnode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!smartnodeSync.IsSynced()) return;

        if(pfrom->nVersion < MIN_MULTIPAYMENT_PROTO_VERSION){
            LogPrint("mnpayments", "SMARTNODEPAYMENTSYNC - peer=%d using not supported version for payment votes %i\n", pfrom->id, pfrom->nVersion);
            connman.PushMessageWithVersion(pfrom, INIT_PROTO_VERSION, NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_MULTIPAYMENT_PROTO_VERSION));
            return;
        }

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if(netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::SMARTNODEPAYMENTSYNC)) {
            LOCK(cs_main);
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("SMARTNODEPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }
        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::SMARTNODEPAYMENTSYNC);

        Sync(pfrom, connman);
        LogPrintf("SMARTNODEPAYMENTSYNC -- Sent Smartnode payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::SMARTNODEPAYMENTVOTE) { // Smartnode Payments Vote for the Winner

        if(pfrom->nVersion < MIN_MULTIPAYMENT_PROTO_VERSION){
            LogPrint("mnpayments", "SMARTNODEPAYMENTVOTE - peer=%d using not supported version for payment votes %i\n", pfrom->id, pfrom->nVersion);
            connman.PushMessageWithVersion(pfrom, INIT_PROTO_VERSION, NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_MULTIPAYMENT_PROTO_VERSION));
            return;
        }

        CSmartnodePaymentVote vote;
        vRecv >> vote;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        // TODO: clear setAskFor for MSG_SMARTNODE_PAYMENT_BLOCK too

        // Ignore any payments messages until smartnode list is synced
        if(!smartnodeSync.IsSmartnodeListSynced()) return;

        {
            LOCK(cs_mapSmartnodePaymentVotes);

            auto res = mapSmartnodePaymentVotes.emplace(nHash, vote);

            // Avoid processing same vote multiple times if it was already verified earlier
            if(!res.second && res.first->second.IsVerified()) {
                LogPrint("mnpayments", "SMARTNODEPAYMENTVOTE -- hash=%s, nBlockHeight=%d/%d seen\n",
                            nHash.ToString(), vote.nBlockHeight, nCachedBlockHeight);
                return;
            }

            res.first->second.MarkAsNotVerified();
        }

        int nFirstBlock = nCachedBlockHeight - GetStorageLimit();

        if(vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > nCachedBlockHeight + MNPAYMENTS_FUTURE_VOTES * 2) {
            LogPrint("mnpaymentvote", "SMARTNODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, nCachedBlockHeight);
            return;
        }

        std::string strError = "";
        if(!vote.IsValid(pfrom, nCachedBlockHeight, strError, connman)) {
            LogPrint("mnpaymentvote", "SMARTNODEPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        smartnode_info_t mnInfo;
        if(!mnodeman.GetSmartnodeInfo(vote.vinSmartnode.prevout, mnInfo)) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrint("mnpaymentvote", "SMARTNODEPAYMENTVOTE -- smartnode is missing %s\n", vote.vinSmartnode.prevout.ToStringShort());
            mnodeman.AskForMN(pfrom, vote.vinSmartnode.prevout, connman);
            return;
        }

        int nDos = 0;
        if(!vote.CheckSignature(mnInfo.pubKeySmartnode, nCachedBlockHeight, nDos)) {
            if(nDos) {
                LOCK(cs_main);
                LogPrint("mnpaymentvote", "SMARTNODEPAYMENTVOTE -- ERROR: invalid signature\n");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("mnpayments", "SMARTNODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            mnodeman.AskForMN(pfrom, vote.vinSmartnode.prevout, connman);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        if(!UpdateLastVote(vote)) {
            LogPrintf("SMARTNODEPAYMENTVOTE -- smartnode already voted, smartnode=%s\n", vote.vinSmartnode.prevout.ToStringShort());
            return;
        }

        for( const auto& payee : vote.payees ){
            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrint("mnpaymentvote", "SMARTNODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s, hash=%s new\n",
                        address2.ToString(), vote.nBlockHeight, nCachedBlockHeight, vote.vinSmartnode.prevout.ToStringShort(), nHash.ToString());
        }

        if(AddOrUpdatePaymentVote(vote)){
            vote.Relay(connman);
            smartnodeSync.BumpAssetLastTime("SMARTNODEPAYMENTVOTE");
        }
    }
}

bool CSmartnodePaymentVote::Sign()
{
    std::string strError;
    std::string strMessage = vinSmartnode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                payees.ToString();

    if(!CMessageSigner::SignMessage(strMessage, vchSig, activeSmartnode.keySmartnode)) {
        LogPrintf("CSmartnodePaymentVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!CMessageSigner::VerifyMessage(activeSmartnode.pubKeySmartnode, vchSig, strMessage, strError)) {
        LogPrintf("CSmartnodePaymentVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CSmartnodePayments::GetBlockPayees(int nBlockHeight, CScriptVector& payees)
{
    LOCK(cs_mapSmartnodeBlocks);

    auto it = mapSmartnodeBlocks.find(nBlockHeight);
    return it != mapSmartnodeBlocks.end() && it->second.GetBestPayees(payees);
}

// Is this smartnode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CSmartnodePayments::IsScheduled(CSmartnode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapSmartnodeBlocks);

    if(!smartnodeSync.IsSmartnodeListSynced()) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScriptVector payees;
    int interval = SmartNodePayments::PayoutInterval(nCachedBlockHeight);

    for(int64_t h = nCachedBlockHeight; h <= nCachedBlockHeight + MNPAYMENTS_FUTURE_VOTES + interval - 1; h++){
        interval = SmartNodePayments::PayoutInterval(h);
        if(h == nNotBlockHeight) continue;
        if(mapSmartnodeBlocks.count(h) &&
           mapSmartnodeBlocks[h].GetBestPayees(payees) &&
           std::find(payees.begin(),payees.end(), mnpayee) != payees.end() ) {
            return true;
        }
    }

    return false;
}

bool CSmartnodePayments::AddOrUpdatePaymentVote(const CSmartnodePaymentVote& vote)
{
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, vote.nBlockHeight - 101)) return false;

    uint256 nVoteHash = vote.GetHash();

    if(HasVerifiedPaymentVote(nVoteHash)) return false;

    LOCK2(cs_mapSmartnodeBlocks, cs_mapSmartnodePaymentVotes);

    mapSmartnodePaymentVotes[nVoteHash] = vote;

    auto it = mapSmartnodeBlocks.emplace(vote.nBlockHeight, CSmartnodeBlockPayees(vote.nBlockHeight)).first;
    it->second.AddPayees(vote);

    LogPrint("mnpayments", "CSmartnodePayments::AddOrUpdatePaymentVote -- added, nHeight=%d, hash=%s\n",it->second.nBlockHeight, nVoteHash.ToString());

    return true;
}


bool CSmartnodePayments::HasVerifiedPaymentVote(uint256 hashIn)
{
    LOCK(cs_mapSmartnodePaymentVotes);
    std::map<uint256, CSmartnodePaymentVote>::iterator it = mapSmartnodePaymentVotes.find(hashIn);
    return it != mapSmartnodePaymentVotes.end() && it->second.IsVerified();
}

void CSmartnodeBlockPayees::AddPayees(const CSmartnodePaymentVote& vote)
{
    LOCK(cs_vecPayees);

    bool found;

    BOOST_FOREACH(const CScript& scriptPubKey, vote.payees)
    {
        found = false;

        BOOST_FOREACH(CSmartnodePayee& payee, vecPayees)
        {
            if (payee.GetPayee() == scriptPubKey) {
                payee.AddVoteHash(vote.GetHash());
                found = true;
                break;
            }
        }

        if(!found){
            CSmartnodePayee payeeNew(scriptPubKey, vote.GetHash());
            vecPayees.push_back(payeeNew);
        }
    }
}

bool CSmartnodeBlockPayees::GetBestPayees(CScriptVector& payeesRet)
{
    LOCK(cs_vecPayees);

    payeesRet.clear();

    size_t expectedPayees = SmartNodePayments::PayoutsPerBlock(nBlockHeight);

    if( !expectedPayees ) {
        LogPrint("mnpayments", "CSmartnodeBlockPayees::GetBestPayee -- ERROR: no payees required here\n");
        return false;
    }

    if(vecPayees.size() < expectedPayees) {
        LogPrint("mnpayments", "CSmartnodeBlockPayees::GetBestPayee -- ERROR: couldn't find enough payees\n");
        return false;
    }

    sort(vecPayees.begin(), vecPayees.end(), CompareBlockPayees());

    BOOST_FOREACH(CSmartnodePayee& payee, vecPayees) {
        LogPrint("mnpayments", "CSmartnodeBlockPayees::GetBestPayee -- Loop votes %d - payeesRet %d\n",payee.GetVoteCount(),payeesRet.size());
        if (payee.GetVoteCount() > -1 ) {
            payeesRet.push_back(payee.GetPayee());
        }

        if( payeesRet.size() == expectedPayees )
            return true;
    }

    return false;
}

bool CSmartnodeBlockPayees::HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq)
{
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CSmartnodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

    LogPrint("mnpaymentvote", "CSmartnodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}

bool CSmartnodeBlockPayees::IsTransactionValid(const CTransaction& txNew, CAmount expectedNodeReward)
{
    LOCK(cs_vecPayees);

    int foundPayees = 0;
    int foundMinVotes = 0;
    int expectedPayees =  SmartNodePayments::PayoutsPerBlock(nBlockHeight);
    std::string strPayeesPossible = "";

    if( !expectedPayees ) return true;

    CAmount expectedPerNode = expectedNodeReward / expectedPayees;

    //require at least MNPAYMENTS_SIGNATURES_REQUIRED signatures

    BOOST_FOREACH(CSmartnodePayee& payee, vecPayees) {
        if( payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED ){
            foundMinVotes++;
            if( foundMinVotes == expectedPayees ) break;
        }
    }

    // if we don't have at least expectedPayees with MNPAYMENTS_SIGNATURES_REQUIRED signatures, approve whichever is the longest chain
    if(!foundMinVotes){
        LogPrintf("CSmartnodeBlockPayees::IsTransactionValid -- WARNING: Approve for too few payees with minimum votes\n");
        return true;
    }

    BOOST_FOREACH(CSmartnodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            BOOST_FOREACH(CTxOut txout, txNew.vout) {
                if (txout.scriptPubKey == payee.GetPayee() && abs(txout.nValue - expectedPerNode) < 2 ) {
                    LogPrint("mnpayments", "CSmartnodeBlockPayees::IsTransactionValid -- Found required payment: %s\n",txout.ToString());
                    foundPayees++;
                    break;
                }
            }

            CTxDestination address1;
            ExtractDestination(payee.GetPayee(), address1);
            CBitcoinAddress address2(address1);

            if(strPayeesPossible == "") {
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    if( foundPayees == foundMinVotes ) return true;

    LogPrintf("CSmartnodeBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s'\n", strPayeesPossible);
    return false;
}

std::string CSmartnodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "Unknown";
    int nInterval = SmartNodePayments::PayoutInterval(nBlockHeight);
    int nPayouts = SmartNodePayments::PayoutsPerBlock(nBlockHeight);

    if( !nInterval || nBlockHeight % nInterval || !nPayouts ) return "NoRewardBlock";

    BOOST_FOREACH(CSmartnodePayee& payee, vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (strRequiredPayments != "Unknown") {
            strRequiredPayments += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        } else {
            strRequiredPayments = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        }
    }

    return strRequiredPayments;
}

UniValue CSmartnodeBlockPayees::GetPaymentBlockObject()
{
    LOCK(cs_vecPayees);

    UniValue obj(UniValue::VOBJ);
    UniValue votes(UniValue::VOBJ);

    int nInterval = SmartNodePayments::PayoutInterval(nBlockHeight);
    int nExpectedPayees = SmartNodePayments::PayoutsPerBlock(nBlockHeight);

    if( !nInterval || nBlockHeight % nInterval || !nExpectedPayees ){
        obj.pushKV("state", "No reward block");
        obj.pushKV("validPayees", 0);
        obj.pushKV("voteSum", 0);
        obj.pushKV("votes", votes);
        return obj;
    }

    int nVoteSum = 0;
    int nValidPayees = 0;

    std::sort(vecPayees.begin(), vecPayees.end(), CompareBlockPayees());

    BOOST_FOREACH(CSmartnodePayee& payee, vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        nVoteSum += payee.GetVoteCount();
        votes.pushKV(address2.ToString(), payee.GetVoteCount());

        if( payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED ){
            nValidPayees++;
        }
    }

    if( !votes.size() ){
        obj.pushKV("state", "No votes");
        obj.pushKV("validPayees", 0);
        obj.pushKV("voteSum", 0);
        obj.pushKV("votes", votes);
        return obj;
    }

    if( nValidPayees >= nExpectedPayees ){
        obj.pushKV("state", "Valid");
    }else{
        obj.pushKV("state", "Not enough valid payees");
    }
    obj.pushKV("validPayees", nValidPayees);
    obj.pushKV("voteSum", nVoteSum);
    obj.pushKV("votes", votes);

    return obj;
}

std::string CSmartnodePayments::GetRequiredPaymentsString(int nHeight)
{
    int nInterval = SmartNodePayments::PayoutInterval(nHeight);
    int nPayouts = SmartNodePayments::PayoutsPerBlock(nHeight);

    if( !nInterval || nHeight % nInterval || !nPayouts ) return "NoRewardBlock";

    LOCK(cs_mapSmartnodeBlocks);

    if(mapSmartnodeBlocks.count(nHeight)){
        return mapSmartnodeBlocks[nHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

UniValue CSmartnodePayments::GetPaymentBlockObject(int nHeight)
{
    int nInterval = SmartNodePayments::PayoutInterval(nHeight);
    int nPayouts = SmartNodePayments::PayoutsPerBlock(nHeight);

    if( !nInterval || nHeight % nInterval || !nPayouts ) return "NoRewardBlock";

    LOCK(cs_mapSmartnodeBlocks);

    if(mapSmartnodeBlocks.count(nHeight)){
        return mapSmartnodeBlocks[nHeight].GetPaymentBlockObject();
    }

    UniValue obj(UniValue::VOBJ);

    obj.pushKV("state", "No votes");
    obj.pushKV("validPayees", 0);
    obj.pushKV("voteSum", 0);
    obj.pushKV("votes", UniValue(UniValue::VARR));

    return obj;
}

bool CSmartnodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight, CAmount expectedNodeReward)
{
    LOCK(cs_mapSmartnodeBlocks);

    if(mapSmartnodeBlocks.count(nBlockHeight)){
        return mapSmartnodeBlocks[nBlockHeight].IsTransactionValid(txNew, expectedNodeReward);
    }

    return true;
}

void CSmartnodePayments::CheckAndRemove()
{
    if(!smartnodeSync.IsSmartNodeSyncStarted()) return;

    LOCK2(cs_mapSmartnodeBlocks, cs_mapSmartnodePaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CSmartnodePaymentVote>::iterator it = mapSmartnodePaymentVotes.begin();
    while(it != mapSmartnodePaymentVotes.end()) {
        CSmartnodePaymentVote vote = (*it).second;

        if(nCachedBlockHeight - vote.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CSmartnodePayments::CheckAndRemove -- Removing old Smartnode payment: nBlockHeight=%d\n", vote.nBlockHeight);
            mapSmartnodePaymentVotes.erase(it++);
            mapSmartnodeBlocks.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
    LogPrintf("CSmartnodePayments::CheckAndRemove -- %s\n", ToString());
}

bool CSmartnodePaymentVote::IsValid(CNode* pnode, int nValidationHeight, std::string& strError, CConnman& connman)
{
    smartnode_info_t mnInfo;

    if(!mnodeman.GetSmartnodeInfo(vinSmartnode.prevout, mnInfo)) {
        strError = strprintf("Unknown Smartnode: prevout=%s", vinSmartnode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Smartnode
        if(smartnodeSync.IsSmartnodeListSynced()) {
            mnodeman.AskForMN(pnode, vinSmartnode.prevout, connman);
        }

        return false;
    }

    int nMinRequiredProtocol;
    if(nBlockHeight >= nValidationHeight) {
        // new votes must comply SPORK_10_SMARTNODE_PAY_UPDATED_NODES rules
        nMinRequiredProtocol = mnpayments.GetMinSmartnodePaymentsProto();
    } else {
        // allow non-updated smartnodes for old blocks
        nMinRequiredProtocol = MIN_SMARTNODE_PAYMENT_PROTO_VERSION_1;
    }

    if(mnInfo.nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Smartnode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", mnInfo.nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only smartnodes should try to check smartnode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify smartnode rank for future block votes only.
    if(!fSmartNode && nBlockHeight < nValidationHeight) return true;

    int nRank;

    if(!mnodeman.GetSmartnodeRank(vinSmartnode.prevout, nRank, nBlockHeight - 101, nMinRequiredProtocol)) {
        LogPrint("mnpayments", "CSmartnodePaymentVote::IsValid -- Can't calculate rank for smartnode %s\n",
                    vinSmartnode.prevout.ToStringShort());
        return false;
    }

    if(nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        // It's common to have smartnodes mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("Smartnode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new mnw which is out of bounds, for old mnw MN list itself might be way too much off
        if(nRank != MNPAYMENTS_NO_RANK && nRank > MNPAYMENTS_SIGNATURES_TOTAL*2 && nBlockHeight > nValidationHeight) {
            LOCK(cs_main);
            strError = strprintf("Smartnode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL*2, nRank);
            LogPrint("mnpayments", "CSmartnodePaymentVote::IsValid -- Error: %s\n", strError);
            Misbehaving(pnode->GetId(), 10);
        }
        // Still invalid however
        return false;
    }

    return true;
}

bool CSmartnodePayments::ProcessBlock(int nBlockHeight, CConnman& connman)
{
    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if(fLiteMode || !fSmartNode) return false;

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about smartnodes.
    if(!smartnodeSync.IsSmartnodeListSynced()) return false;

    int nRank;

    if (!mnodeman.GetSmartnodeRank(activeSmartnode.outpoint, nRank, nBlockHeight - 101, GetMinSmartnodePaymentsProto())) {
        LogPrint("mnpayments", "CSmartnodePayments::ProcessBlock -- Unknown Smartnode\n");
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CSmartnodePayments::ProcessBlock -- Smartnode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }

    // LOCATE THE NEXT SMARTNODE WHICH SHOULD BE PAID

    LogPrintf("CSmartnodePayments::ProcessBlock -- Start: nBlockHeight=%d, smartnode=%s\n", nBlockHeight, activeSmartnode.outpoint.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CSmartNodeWinners mnInfos;

    if (!mnodeman.GetNextSmartnodesInQueueForPayment(nBlockHeight, true, nCount, mnInfos)) {
        LogPrintf("CSmartnodePayments::ProcessBlock -- ERROR: Failed to find smartnode to pay\n");
        return false;
    }

    CScriptVector payees;

    for(const auto& mnInfo : mnInfos ){
        LogPrintf("CSmartnodePayments::ProcessBlock -- Smartnode found by GetNextSmartnodeInQueueForPayment(): %s\n", mnInfo.vin.ToString());
        payees.push_back(GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID()));
    }

    CSmartnodePaymentVote voteNew(activeSmartnode.outpoint, nBlockHeight, payees);

    for(const auto& payee : payees ){

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrintf("CSmartnodePayments::ProcessBlock -- vote: payee=%s, nBlockHeight=%d\n", address2.ToString(), nBlockHeight);
    }

    // SIGN MESSAGE TO NETWORK WITH OUR SMARTNODE KEYS

    LogPrintf("CSmartnodePayments::ProcessBlock -- Signing vote\n");
    if (voteNew.Sign()) {
        LogPrintf("CSmartnodePayments::ProcessBlock -- AddOrUpdatePaymentVote()\n");

        if (AddOrUpdatePaymentVote(voteNew)) {
            voteNew.Relay(connman);
            return true;
        }
    }

    return false;
}

// TBD Check -> This is not very useful? Why should we do that?
//void CSmartnodePayments::CheckPreviousBlockVotes(int nPrevBlockHeight)
//{
//    if (!smartnodeSync.IsWinnersListSynced()) return;

//    std::string debugStr;

//    debugStr += strprintf("CSmartnodePayments::CheckPreviousBlockVotes -- nPrevBlockHeight=%d, expected voting MNs:\n", nPrevBlockHeight);

//    CSmartnodeMan::rank_pair_vec_t mns;
//    if (!mnodeman.GetSmartnodeRanks(mns, nPrevBlockHeight - 101, GetMinSmartnodePaymentsProto())) {
//        debugStr += "CSmartnodePayments::CheckPreviousBlockVotes -- GetSmartnodeRanks failed\n";
//        LogPrint("mnpayments", "%s", debugStr);
//        return;
//    }

//    LOCK2(cs_mapSmartnodeBlocks, cs_mapSmartnodePaymentVotes);

//    for (int i = 0; i < MNPAYMENTS_SIGNATURES_TOTAL && i < (int)mns.size(); i++) {
//        auto mn = mns[i];
//        CScript payee;
//        bool found = false;

//        if (mapSmartnodeBlocks.count(nPrevBlockHeight)) {
//            for (auto &p : mapSmartnodeBlocks[nPrevBlockHeight].vecPayees) {
//                for (auto &voteHash : p.GetVoteHashes()) {
//                    if (!mapSmartnodePaymentVotes.count(voteHash)) {
//                        debugStr += strprintf("CSmartnodePayments::CheckPreviousBlockVotes --   could not find vote %s\n",
//                                              voteHash.ToString());
//                        continue;
//                    }
//                    auto vote = mapSmartnodePaymentVotes[voteHash];
//                    if (vote.vinSmartnode.prevout == mn.second.vin.prevout) {
//                        payee = vote.payee;
//                        found = true;
//                        break;
//                    }
//                }
//            }
//        }

//        if (!found) {
//            debugStr += strprintf("CSmartnodePayments::CheckPreviousBlockVotes --   %s - no vote received\n",
//                                  mn.second.vin.prevout.ToStringShort());
//            mapSmartnodesDidNotVote[mn.second.vin.prevout]++;
//            continue;
//        }

//        CTxDestination address1;
//        ExtractDestination(payee, address1);
//        CBitcoinAddress address2(address1);

//        debugStr += strprintf("CSmartnodePayments::CheckPreviousBlockVotes --   %s - voted for %s\n",
//                              mn.second.vin.prevout.ToStringShort(), address2.ToString());
//    }
//    debugStr += "CSmartnodePayments::CheckPreviousBlockVotes -- Smartnodes which missed a vote in the past:\n";
//    for (auto it : mapSmartnodesDidNotVote) {
//        debugStr += strprintf("CSmartnodePayments::CheckPreviousBlockVotes --   %s: %d\n", it.first.ToStringShort(), it.second);
//    }

//    LogPrint("mnpayments", "%s", debugStr);
//}

void CSmartnodePaymentVote::Relay(CConnman& connman)
{
    // Do not relay until fully synced
    if(!smartnodeSync.IsSynced()) {
        LogPrint("mnpaymentvote", "CSmartnodePayments::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_SMARTNODE_PAYMENT_VOTE, GetHash());
    connman.RelayInv(inv);
}

bool CSmartnodePaymentVote::CheckSignature(const CPubKey& pubKeySmartnode, int nValidationHeight, int &nDos)
{
    // do not ban by default
    nDos = 0;

    std::string strMessage = vinSmartnode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                payees.ToString();

    std::string strError = "";
    if (!CMessageSigner::VerifyMessage(pubKeySmartnode, vchSig, strMessage, strError)) {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if(smartnodeSync.IsSmartnodeListSynced() && nBlockHeight > nValidationHeight) {
            nDos = 20;
        }
        return error("CSmartnodePaymentVote::CheckSignature -- Got bad Smartnode payment signature, smartnode=%s, error: %s", vinSmartnode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

std::string CSmartnodePaymentVote::ToString() const
{
    std::ostringstream info;

    info << vinSmartnode.prevout.ToStringShort() <<
            ", " << nBlockHeight <<
            ", " << payees.ToString() <<
            ", " << (int)vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CSmartnodePayments::Sync(CNode* pnode, CConnman& connman)
{
    LOCK(cs_mapSmartnodeBlocks);

    if(!smartnodeSync.IsWinnersListSynced()) return;

    int nInvCount = 0;

    for(int h = nCachedBlockHeight; h < nCachedBlockHeight + (MNPAYMENTS_FUTURE_VOTES * 2); h++) {
        if(mapSmartnodeBlocks.count(h)) {
            BOOST_FOREACH(CSmartnodePayee& payee, mapSmartnodeBlocks[h].vecPayees) {
                std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
                BOOST_FOREACH(uint256& hash, vecVoteHashes) {
                    if(!HasVerifiedPaymentVote(hash)) continue;
                    pnode->PushInventory(CInv(MSG_SMARTNODE_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CSmartnodePayments::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    connman.PushMessage(pnode, NetMsgType::SYNCSTATUSCOUNT, SMARTNODE_SYNC_MNW, nInvCount);
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CSmartnodePayments::RequestLowDataPaymentBlocks(CNode* pnode, CConnman& connman)
{
    if(!smartnodeSync.IsSmartnodeListSynced()) return;

    LOCK2(cs_main, cs_mapSmartnodeBlocks);

    std::vector<CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = chainActive.Tip();

    while(nCachedBlockHeight - pindex->nHeight < nLimit) {

        int nInterval = SmartNodePayments::PayoutInterval(pindex->nHeight);

        const auto it = mapSmartnodeBlocks.find(pindex->nHeight);
        if(it == mapSmartnodeBlocks.end() && nInterval && pindex->nHeight % nInterval) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_SMARTNODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if(vToFetch.size() == MAX_INV_SZ) {
                LogPrintf("CSmartnodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d blocks\n", pnode->id, MAX_INV_SZ);
                connman.PushMessage(pnode, NetMsgType::GETDATA, vToFetch);
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if(!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    std::map<int, CSmartnodeBlockPayees>::iterator it = mapSmartnodeBlocks.begin();

    while(it != mapSmartnodeBlocks.end()) {
        int nExpectedPayees = SmartNodePayments::PayoutsPerBlock(it->first);
        int nInterval = SmartNodePayments::PayoutInterval(it->first);
        int nTotalVotes = 0;
        int fFound = 0;
        BOOST_FOREACH(CSmartnodePayee& payee, it->second.vecPayees) {
            if(payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
                if( ++fFound == nExpectedPayees) break;
            }
            nTotalVotes += payee.GetVoteCount();
        }
        // A clear winner (MNPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
        // or no clear winner was found but there are at least avg number of votes
        if(fFound == nExpectedPayees ||
           nTotalVotes >= ( (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) * nExpectedPayees )/2 ||
           !nInterval || it->first % nInterval) {
            // so just move to the next block
            ++it;
            continue;
        }
        // DEBUG
        DBG (
            // Let's see why this failed
            BOOST_FOREACH(CSmartnodePayee& payee, it->second.vecPayees) {
                CTxDestination address1;
                ExtractDestination(payee.GetPayee(), address1);
                CBitcoinAddress address2(address1);
                printf("payee %s votes %d\n", address2.ToString().c_str(), payee.GetVoteCount());
            }
            printf("block %d votes total %d\n", it->first, nTotalVotes);
        )
        // END DEBUG
        // Low data block found, let's try to sync it
        uint256 hash;
        if(GetBlockHash(hash, it->first)) {
            vToFetch.push_back(CInv(MSG_SMARTNODE_PAYMENT_BLOCK, hash));
        }
        // We should not violate GETDATA rules
        if(vToFetch.size() == MAX_INV_SZ) {
            LogPrintf("CSmartnodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, MAX_INV_SZ);
            connman.PushMessage(pnode, NetMsgType::GETDATA, vToFetch);
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if(!vToFetch.empty()) {
        LogPrintf("CSmartnodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, vToFetch.size());
        connman.PushMessage(pnode, NetMsgType::GETDATA, vToFetch);
    }
}

std::string CSmartnodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapSmartnodePaymentVotes.size() <<
            ", Blocks: " << (int)mapSmartnodeBlocks.size();

    return info.str();
}

bool CSmartnodePayments::IsEnoughData()
{
    int expectedPayees = SmartNodePayments::PayoutsPerBlock(nCachedBlockHeight);
    float nAverageVotes = ((MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) * expectedPayees) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CSmartnodePayments::GetStorageLimit()
{
    return std::max(int(mnodeman.size() * nStorageCoeff), nMinBlocksToStore);
}

void CSmartnodePayments::UpdatedBlockTip(const CBlockIndex *pindex, CConnman& connman)
{
    if(!pindex) return;

    nCachedBlockHeight = pindex->nHeight;
    LogPrint("mnpayments", "CSmartnodePayments::UpdatedBlockTip -- nCachedBlockHeight=%d\n", nCachedBlockHeight);

    int interval = SmartNodePayments::PayoutInterval(nCachedBlockHeight);
    int nFutureBlock = nCachedBlockHeight + MNPAYMENTS_FUTURE_VOTES + interval;

//    CheckPreviousBlockVotes(nFutureBlock - 1);
    if( interval && !(nFutureBlock % interval) ) ProcessBlock(nFutureBlock, connman);
}
