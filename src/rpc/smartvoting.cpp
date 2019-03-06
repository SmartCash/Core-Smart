// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 The SmartCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "core_io.h"
#include "coincontrol.h"
#include "consensus/validation.h"
#include "init.h"
#include "validation.h"
#include "messagesigner.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "smartnode/smartnodesync.h"
#include "smartmining/miningpayments.h"
#include "smartvoting/proposal.h"
#include "smartvoting/manager.h"
#include "smartvoting/votekeys.h"
#include "smartvoting/votevalidation.h"
#include "smartvoting/voting.h"
#include "util.h"
#include "wallet/wallet.h"
#include <univalue.h>

int64_t nVotingUnlockTime;
static CCriticalSection cs_nVotingUnlockTime;

extern UniValue UniValueFromAmount(int64_t nAmount);
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);

static void LockVoting(CWallet* pWallet)
{
    LOCK(cs_nVotingUnlockTime);
    nVotingUnlockTime = 0;
    pWallet->LockVoting();
}

void EnsureVotingIsUnlocked()
{
    if (pwalletMain->IsVotingLocked())
        throw JSONRPCError(RPC_VOTEKEYS_UNLOCK_NEEDED, "Error: Voting storage encrypted and locked. Use \"votekeys unlock\" first to unlock.");
}

UniValue ParseJSON(const std::string& strVal)
{
    UniValue jVal;
    if (!jVal.read(std::string("[")+strVal+std::string("]")) ||
        !jVal.isArray() || jVal.size()!=1)
        throw runtime_error(string("Error parsing JSON:")+strVal);
    return jVal[0];
}

UniValue sendVote( const CVoteKeySecret &voteKeySecret, const uint256 &hash, const std::string strVoteSignal, const std::string strVoteOutcome )
{
    CVoteKey voteKey(voteKeySecret.GetKey().GetPubKey().GetID());

    // CONVERT NAMED SIGNAL/ACTION AND CONVERT

    vote_signal_enum_t eVoteSignal = CProposalVoting::ConvertVoteSignal(strVoteSignal);
    if(eVoteSignal == VOTE_SIGNAL_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid vote signal. Please using one of the following: "
                           "(funding|valid|delete|endorsed)");
    }

    vote_outcome_enum_t eVoteOutcome = CProposalVoting::ConvertVoteOutcome(strVoteOutcome);
    if(eVoteOutcome == VOTE_OUTCOME_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vote outcome. Please use one of the following: 'yes', 'no' or 'abstain'");
    }

    // EXECUTE VOTE FOR EACH MASTERNODE, COUNT SUCCESSES VS FAILURES

    UniValue resultsObj(UniValue::VOBJ);
    UniValue statusObj(UniValue::VOBJ);

    // CREATE NEW PROPOSAL VOTE WITH OUTCOME/SIGNAL

    CProposalVote vote(voteKey, hash, eVoteSignal, eVoteOutcome);
    if(vote.Sign(voteKeySecret)) {

        CSmartVotingException exception;
        if(smartVoting.ProcessVoteAndRelay(vote, exception, *g_connman)) {
            statusObj.pushKV("result", "success");
        } else {
            statusObj.pushKV("result", "failed");
            statusObj.pushKV("errorMessage", exception.GetMessage());
        }

        resultsObj.pushKV(voteKey.ToString(), statusObj);

    }else{

        statusObj.pushKV("result", "failed");
        statusObj.pushKV("errorMessage", "Failure to sign.");
        resultsObj.pushKV(voteKey.ToString(), statusObj);
    }

    return resultsObj;

}

UniValue smartvoting(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    std::vector<std::string> vecCommands = {
        "check",
#ifdef ENABLE_WALLET
        "prepare",
#endif // ENABLE_WALLET
        "submit",
        "deserialize",
        "count",
        "list",
        "get",
        "getvotes",
        "voteraw",
        "votewithkey",
        "vote"
    };

    if (fHelp  || std::find(vecCommands.begin(), vecCommands.end(), strCommand) == vecCommands.end() )
        throw std::runtime_error(
                "smartvoting \"command\"...\n"
                "Use SmartVoting commands.\n"
                "\nAvailable commands:\n"
                "  check              - Validate raw proposal data\n"
                "  prepare            - Create and prepare a proposal by signing and creating the fee tx\n"
                "  submit             - Submit a proposal to the network\n"
                "  count              - Count proposals.\n"
                "  list               - List all proposals.\n"
                "  get                - Get a proposal by its hash\n"
                "  getvotes           - Get all votes for a proposal\n"
                "  voteraw            - Broadcast a raw signed vote\n"
                "  votewithkey        - Vote for a proposal with a specific votekey\n"
                "  vote               - Vote for a proposal with votekeys available in the votekey storage\n"
                );

    if(strCommand == "check")
    {
        if (params.size() != 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartvoting check <data-hex>'");
        }

        std::string strRawProposal = params[1].get_str();

        if (!IsHex(strRawProposal))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid proposal data. Must be hex-string");

        vector<unsigned char> rawData(ParseHex(strRawProposal));

        CDataStream ssProposal( rawData ,SER_NETWORK, PROTOCOL_VERSION);

        CProposal proposal;

        ssProposal >> proposal;

        UniValue objResult(UniValue::VOBJ);

        std::string result = "OK";
        int nMissingConfirmations;
        std::string strError;
        {
            LOCK(cs_main);

            if( !proposal.IsValidLocally(strError, nMissingConfirmations, true) ){
                result = strError;
            }
        }

        objResult.pushKV("Proposal status", result);

        UniValue bObj(UniValue::VOBJ);
        bObj.pushKV("Hash",  proposal.GetHash().ToString());
        bObj.pushKV("FeeHash",  proposal.GetFeeHash().ToString());
        bObj.pushKV("Title",  proposal.GetTitle());
        bObj.pushKV("Url",  proposal.GetUrl());
        bObj.pushKV("CreationTime", proposal.GetCreationTime());
        const CSmartAddress& proposalAddress = proposal.GetAddress();
        if(proposalAddress.IsValid()) {
            bObj.pushKV("ProposalAddress", proposalAddress.ToString());
        }else{
            bObj.pushKV("ProposalAddress", "Invalid");
        }

        objResult.pushKV("Data", bObj);

        return objResult;
    }



    // PREPARE THE PROPOSAL BY CREATING A COLLATERAL TRANSACTION
    if(strCommand == "prepare")
    {
        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        if (params.size() != 5) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartvoting prepare <title> <url> <address> <milestone-array>'");
        }

        int64_t nCreationTime = GetAdjustedTime();

        CInternalProposal proposal;

        proposal.SetTitle(params[1].get_str());
        proposal.SetUrl(params[2].get_str());
        proposal.SetAddress(CSmartAddress(params[3].get_str()));

        for( UniValue milestone : ParseJSON(params[4].get_str()).get_array().getValues() ){

            if( !milestone.isObject() ||
                !milestone.exists("timestamp") || !milestone["timestamp"].isNum() ||
                !milestone.exists("amount") || !milestone["amount"].isNum() ||
                !milestone.exists("description") || !milestone["description"].isStr() ){
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct milestones format is: [{\"timestamp\" : <unix timestamp>, \"amount\" : <amount USD>, \"description\" : <description>},{...},..]");
            }

            CProposalMilestone m(milestone["timestamp"].get_int64(), milestone["amount"].get_int64(), milestone["description"].get_str() );

            proposal.AddMilestone(m);
        }

        std::vector<std::string> vecErrors;

        proposal.SetCreationTime(nCreationTime);

        if( !proposal.IsValid(vecErrors) ){

            std::string strError;

            for( auto error : vecErrors ){
                strError += error + "\n";
            }

            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid proposal data, error messages: " + strError);
        }

        LOCK2(cs_main, pwalletMain->cs_wallet);

        EnsureWalletIsUnlocked();

        CWalletTx wtx;
        if(!pwalletMain->GetProposalFeeTX(wtx, proposal.GetAddress(), proposal.GetHash(), SMARTVOTING_PROPOSAL_FEE)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to create the proposal transaction. Please check the balance of the provided proposal address.");
        }

        CReserveKey reservekey(pwalletMain);
        if (!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get(), NetMsgType::TX)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to send the proposal transaction to the network! Check your connection.");
        }

        // Create the signature of the proposal hash as proof of ownership for
        // the voting portal.

        CKeyID keyID;
        if (!proposal.GetAddress().GetKeyID(keyID)){
            throw JSONRPCError(RPC_INTERNAL_ERROR,"The selected proposal address doesn't refer to a key.");
        }

        CKey key;
        if (!pwalletMain->GetKey(keyID, key)){
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Private key for the proposal address is not available.");
        }

        CDataStream ss(SER_GETHASH, 0);
        ss << strMessageMagic;
        ss << proposal.GetHash().ToString();

        std::vector<unsigned char> vchSig;
        if (!key.SignCompact(Hash(ss.begin(), ss.end()), vchSig)){
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Message signing failed.");
        }

        // Store the signed hash for register in the voting portal
        proposal.SetSignedHash(EncodeBase64(&vchSig[0], vchSig.size()));

        // Set the created tx as proposal fee tx
        proposal.SetFeeHash(wtx.GetHash());
        proposal.SetRawFeeTx(EncodeHexTx(wtx));

        CDataStream ssProposal(SER_NETWORK, PROTOCOL_VERSION);
        ssProposal << static_cast<CProposal>(proposal);

        DBG( std::cout << "smartvoting: prepare "
             << " GetDataAsPlainString = " << proposal.GetDataAsPlainString()
             << ", hash = " << proposal.GetHash().GetHex()
             << ", txidFee = " << wtx.GetHash().GetHex()
             << std::endl; );

        UniValue obj(UniValue::VOBJ);

        obj.pushKV("feeTxHash", wtx.GetHash().ToString());
        obj.pushKV("proposalHash", proposal.GetHash().ToString());
        obj.pushKV("signedHash", proposal.GetSignedHash());
        obj.pushKV("rawProposal", HexStr(ssProposal.begin(), ssProposal.end()));

        return obj;
    }

    // AFTER COLLATERAL TRANSACTION HAS MATURED USER CAN SUBMIT GOVERNANCE OBJECT TO PROPAGATE NETWORK
    if(strCommand == "submit")
    {
        if (params.size() != 2)  {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartvoting submit <raw-proposal>'");
        }

        if(!smartnodeSync.IsSynced()) {
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Must wait for client to sync with smartnode network. Try again in a few minutes.");
        }

        std::string strRawProposal = params[1].get_str();

        if (!IsHex(strRawProposal))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid proposal data. Must be hex-string");

        vector<unsigned char> rawData(ParseHex(strRawProposal));

        CDataStream ssProposal( rawData ,SER_NETWORK, PROTOCOL_VERSION);

        CProposal proposal;

        ssProposal >> proposal;

        // GET THE PARAMETERS FROM USER

        DBG( std::cout << "smartvoting: submit "
             << " GetDataAsPlainString = " << proposal.GetDataAsPlainString()
             << ", hash = " << proposal.GetHash().ToString()
             << ", txidFee = " << proposal.GetFeeHash().ToString()
             << std::endl; );

        std::string strHash = proposal.GetHash().ToString();

        std::string strError = "";
        int fMissingConfirmations;
        {
            LOCK(cs_main);

            bool fIsValid = proposal.IsValidLocally(strError, fMissingConfirmations, true);
            if(!fIsValid){
                LogPrintf("smartvoting(submit) -- Proposal submission rejected because proposal is not valid - hash = %s, strError = %s\n", strHash, strError);
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Proposal is not valid - " + strHash + " - " + strError);
            }
        }

        LogPrintf("smartvoting(submit) -- Adding locally created proposal - %s\n", strHash);

        if(fMissingConfirmations > 0) {
            smartVoting.AddPostponedProposal(proposal);
            proposal.Relay(*g_connman);
        } else {
            smartVoting.AddProposal(proposal, *g_connman);
        }

        UniValue obj(UniValue::VOBJ);

        obj.pushKV("status", fMissingConfirmations > 0 ? strError : "OK");
        obj.pushKV("proposalHash", proposal.GetHash().ToString());

        return obj;
    }

    if(strCommand == "count") {
        std::string strMode{"json"};

        if (params.size() == 2) {
            strMode = params[1].get_str();
        }

        if (params.size() > 2 || (strMode != "json" && strMode != "all")) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartvoting count ( \"json\"|\"all\" )'");
        }

        return strMode == "json" ? smartVoting.ToJson() : smartVoting.ToString();
    }

    // USERS CAN QUERY THE SYSTEM FOR A LIST OF PROPOSALS
    if(strCommand == "list")
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartvoting list [active|all]'");

        // GET MAIN PARAMETER FOR THIS MODE, VALID OR ALL?

        std::string strType = params[1].get_str();
        if (strType != "active" && strType != "all")
             throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid type, should be 'active' or 'all'");

        // SETUP BLOCK INDEX VARIABLE / RESULTS VARIABLE

        UniValue objResult(UniValue::VOBJ);

        // GET MATCHING GOVERNANCE OBJECTS

        LOCK2(cs_main, smartVoting.cs);

        std::vector<const CProposal*> objs = smartVoting.GetAllNewerThan(0);

        // CREATE RESULTS FOR USER

        for (const auto& pProposal : objs)
        {
            //if(strType == "active" && !pProposal->IsOpenToVote()) continue;

            UniValue bObj(UniValue::VOBJ);
            bObj.pushKV("Hash",  pProposal->GetHash().ToString());
            bObj.pushKV("FeeHash",  pProposal->GetFeeHash().ToString());
            bObj.pushKV("Title",  pProposal->GetTitle());
            bObj.pushKV("Url",  pProposal->GetUrl());
            bObj.pushKV("CreationTime", pProposal->GetCreationTime());
            bObj.pushKV("CreationHeight", pProposal->GetVotingStartHeight());
            const CSmartAddress& proposalAddress = pProposal->GetAddress();
            if(proposalAddress.IsValid()) {
                bObj.pushKV("ProposalAddress", proposalAddress.ToString());
            }else{
                bObj.pushKV("ProposalAddress", "Invalid");
            }
            bObj.pushKV("ValidityEndHeight", pProposal->GetValidVoteEndHeight());
            bObj.pushKV("FundingEndHeight", pProposal->GetFundingVoteEndHeight());

            // REPORT STATUS FOR FUNDING VOTES SPECIFICALLY
            CVoteResult fundingResult = pProposal->GetVotingResult(VOTE_SIGNAL_FUNDING);
            bObj.pushKV("YesPower", fundingResult.nYesPower);
            bObj.pushKV("NoPower", fundingResult.nNoPower);
            bObj.pushKV("AbstainPower", fundingResult.nAbstainPower);
            bObj.pushKV("YesPercent", fundingResult.percentYes);
            bObj.pushKV("NoPercent", fundingResult.percentNo);
            bObj.pushKV("AbstainPercent", fundingResult.percentAbstain);

            // REPORT VALIDITY AND CACHING FLAGS FOR VARIOUS SETTINGS
            std::string strError = "";
            bObj.pushKV("fBlockchainValidity",  pProposal->IsValidLocally(strError, false));
            bObj.pushKV("IsValidReason",  strError.c_str());
            bObj.pushKV("fCachedValid",  pProposal->IsSetCachedValid());
            bObj.pushKV("fCachedFunding",  pProposal->IsSetCachedFunding());

            objResult.pushKV(pProposal->GetHash().ToString(), bObj);
        }

        return objResult;
    }

    // GET SPECIFIC GOVERNANCE ENTRY
    if(strCommand == "get")
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartvoting get <proposal-hash>'");

        // COLLECT VARIABLES FROM OUR USER
        uint256 hash = ParseHashV(params[1], "Proposal hash");

        LOCK2(cs_main, smartVoting.cs);

        // FIND THE GOVERNANCE OBJECT THE USER IS LOOKING FOR
        CProposal* pProposal = smartVoting.FindProposal(hash);

        if(pProposal == NULL)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown governance object");

        // REPORT BASIC OBJECT STATS

        UniValue objResult(UniValue::VOBJ);
        objResult.pushKV("Hash",  pProposal->GetHash().ToString());
        objResult.pushKV("FeeHash",  pProposal->GetFeeHash().ToString());
        objResult.pushKV("Title",  pProposal->GetTitle());
        objResult.pushKV("Url",  pProposal->GetUrl());
        objResult.pushKV("CreationTime", pProposal->GetCreationTime());
        objResult.pushKV("CreationHeight", pProposal->GetVotingStartHeight());
        const CSmartAddress& proposalAddress = pProposal->GetAddress();
        if(proposalAddress.IsValid()) {
            objResult.pushKV("ProposalAddress", proposalAddress.ToString());
        }else{
            objResult.pushKV("ProposalAddress", "Invalid");
        }
        objResult.pushKV("ValidityEndHeight", pProposal->GetValidVoteEndHeight());
        objResult.pushKV("FundingEndHeight", pProposal->GetFundingVoteEndHeight());

        // SHOW (MUCH MORE) INFORMATION ABOUT VOTES FOR GOVERNANCE OBJECT (THAN LIST/DIFF ABOVE)
        // -- FUNDING VOTING RESULTS

        UniValue objFunding(UniValue::VOBJ);
        CVoteResult fundingResult = pProposal->GetVotingResult(VOTE_SIGNAL_FUNDING);
        objFunding.pushKV("YesPower", fundingResult.nYesPower);
        objFunding.pushKV("NoPower", fundingResult.nNoPower);
        objFunding.pushKV("AbstainPower", fundingResult.nAbstainPower);
        objFunding.pushKV("YesPercent", fundingResult.percentYes);
        objFunding.pushKV("NoPercent", fundingResult.percentNo);
        objFunding.pushKV("AbstainPercent", fundingResult.percentAbstain);
        objResult.pushKV("FundingResult", objFunding);

        // -- VALIDITY VOTING RESULTS
        UniValue objValid(UniValue::VOBJ);
        CVoteResult validResult = pProposal->GetVotingResult(VOTE_SIGNAL_VALID);
        objValid.pushKV("YesPower", validResult.nYesPower);
        objValid.pushKV("NoPower", validResult.nNoPower);
        objValid.pushKV("AbstainPower", validResult.nAbstainPower);
        objValid.pushKV("YesPercent", validResult.percentYes);
        objValid.pushKV("NoPercent", validResult.percentNo);
        objValid.pushKV("AbstainPercent", validResult.percentAbstain);
        objResult.pushKV("ValidResult", objValid);


        // --
        std::string strError = "";
        objResult.pushKV("fLocalValidity",  pProposal->IsValidLocally(strError, false));
        objResult.pushKV("IsValidReason",  strError.c_str());
        objResult.pushKV("fCachedValid",  pProposal->IsSetCachedValid());
        objResult.pushKV("fCachedFunding",  pProposal->IsSetCachedFunding());
        return objResult;
    }

    // GETVOTES FOR SPECIFIC GOVERNANCE OBJECT
    if(strCommand == "getvotes")
    {
        if (params.size() != 2)
            throw std::runtime_error(
                "Correct usage is 'smartvoting getvotes <proposal-hash>'"
                );

        // COLLECT PARAMETERS FROM USER

        uint256 hash = ParseHashV(params[1], "Proposal hash");

        // FIND OBJECT USER IS LOOKING FOR

        LOCK(smartVoting.cs);

        CProposal* pProposal = smartVoting.FindProposal(hash);

        if(pProposal == NULL) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown proposal-hash");
        }

        // REPORT RESULTS TO USER

        UniValue bResult(UniValue::VARR);

        // GET MATCHING VOTES BY HASH, THEN SHOW USERS VOTE INFORMATION

        std::vector<CProposalVote> vecVotes = smartVoting.GetMatchingVotes(hash);
        for (const auto& vote : vecVotes) {
            UniValue objVote(UniValue::VOBJ);

            objVote.pushKV("hash", vote.GetHash().ToString());
            objVote.pushKV("voteKey", vote.GetVoteKey().ToString());
            objVote.pushKV("time", vote.GetTimestamp());
            objVote.pushKV("type",CProposalVoting::ConvertSignalToString(vote.GetSignal()));
            objVote.pushKV("voted", CProposalVoting::ConvertOutcomeToString(vote.GetOutcome()));

            UniValue objPower(UniValue::VOBJ);

            CVotingPower power;

            GetVotingPower(vote.GetVoteKey(), power);

            objPower.pushKV("address", power.address.ToString());
            objPower.pushKV("height", power.nBlockHeight);
            objPower.pushKV("power", power.nPower);

            objVote.pushKV("power", objPower);
            bResult.push_back(objVote);
        }

        return bResult;
    }

    if(strCommand == "voteraw")
    {
        if(params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartvoting voteraw <raw-vote-data>'");

        std::string strRawVote = params[1].get_str();

        if (!IsHex(strRawVote))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vote data. Must be hex-string");

        vector<unsigned char> rawData(ParseHex(strRawVote));

        CDataStream ssProposal( rawData ,SER_NETWORK, PROTOCOL_VERSION);

        CProposalVote vote;

        ssProposal >> vote;

        UniValue resultsObj(UniValue::VOBJ);
        UniValue statusObj(UniValue::VOBJ);

        if( vote.CheckSignature() ) {

            CSmartVotingException exception;
            if(smartVoting.ProcessVoteAndRelay(vote, exception, *g_connman)) {
                statusObj.pushKV("result", "success");
            } else {
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("errorMessage", exception.GetMessage());
            }

            resultsObj.pushKV(vote.GetVoteKey().ToString(), statusObj);

        }else{

            statusObj.pushKV("result", "failed");
            statusObj.pushKV("errorMessage", "Invalid signature.");
            resultsObj.pushKV(vote.GetVoteKey().ToString(), statusObj);
        }
    }

    if(strCommand == "votewithkey")
    {
        if(params.size() != 5)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartvoting votewithkey <proposal-hash> [funding|valid] [yes|no|abstain] <vote-key-secret>'");

        uint256 hash = ParseHashV(params[1], "Proposal hash");
        std::string strVoteSignal = params[2].get_str();
        std::string strVoteOutcome = params[3].get_str();

        CVoteKeySecret voteKeySecret;

        if( !voteKeySecret.SetString(params[4].get_str()) )
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Invalid <vote-key-secret>: %s", params[4].get_str()));

        return sendVote(voteKeySecret, hash, strVoteSignal, strVoteOutcome);
    }

    if(strCommand == "vote")
    {
        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        EnsureVotingIsUnlocked();

        if(params.size() != 5)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'smartvoting vote <proposal-hash> [funding|valid] [yes|no|abstain] enabledVoteKeysOnly'");

        uint256 hash = ParseHashV(params[1], "Proposal hash");
        std::string strVoteSignal = params[2].get_str();
        std::string strVoteOutcome = params[3].get_str();
        bool fEnabledOnly = ParseJSON(params[4].get_str()).get_bool();

        std::set<CKeyID> setVoteKeyIds;

        pwalletMain->GetVotingKeys(setVoteKeyIds);

        UniValue result(UniValue::VARR);
        for( auto keyId : setVoteKeyIds ){
            if( fEnabledOnly && !pwalletMain->mapVotingKeyMetadata[keyId].fEnabled) continue;
            CKey secret;
            pwalletMain->GetVotingKey(keyId, secret);
            result.push_back(sendVote(secret, hash, strVoteSignal, strVoteOutcome));
        }

        return result;
    }

    return NullUniValue;
}

UniValue votekeys(const UniValue& params, bool fHelp)
{

    std::string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    std::vector<std::string> vecCommands = {
        "list",
        "count",
        "get",
        "encrypt",
        "changepassphrase",
        "unlock",
        "lock",
        "register",
        "import",
        "available",
        "update",
        "export",
    };

    if( std::find(vecCommands.begin(), vecCommands.end(), strCommand) == vecCommands.end() )
        throw std::runtime_error(
           "votekeys \"command\"...\n"
           "Commands to manage your SmartCash VoteKeys.\n"
           "\nGlobal VoteKeys:\n"
           "  list               - List all registered votekeys\n"
           "  count              - Count all registered votekeys\n"
           "  get                - Get the registration information about a votekey or an address\n"
           "\nVoting storage encryption:\n"
           "  encrypt            - Encrypt the voting storage with a voting only password\n"
           "  changepassphrase   - Change the voting encryption password\n"
           "  unlock             - Unlock the encrypted voting storage\n"
           "  lock               - Lock the unlocked and encrypted voting storage\n"
           "\nVoting storage management:\n"
           "  register           - Register a SmartCash address for voting\n"
           "  import             - Import a VoteKey secret to your wallet\n"
           "  available          - Show all available VoteKeys\n"
           "  update             - Enable/Disable a specific VoteKey for voting\n"
           "  export             - Export all available VoteKeys with their secrets\n"
           );

    if(strCommand == "register")
    {
        if (params.size() != 4)  {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'votekeys register <address> <txhash> <index>' where <txhash> and <index> should describe an unspent output used to register with at least 1.002 SMART");
        }

        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        LOCK2(cs_main, pwalletMain->cs_wallet);

        EnsureWalletIsUnlocked();
        EnsureVotingIsUnlocked();

        CVoteKey voteKey;
        unsigned char cRegisterOption = 0x01;

        uint256 txHash = uint256S(params[2].get_str());
        int64_t txIndex = ParseJSON(params[3].get_str()).get_int64();

        // **
        // Check if the unspent output belongs to <address> or not
        // **

        CTransaction spendTx;
        uint256 blockHash;

        if( !GetTransaction(txHash, spendTx, Params().GetConsensus(), blockHash, true) )
            throw JSONRPCError(RPC_INVALID_PARAMETER, "<txhash> doesn't belong to a transaction");

        if( txIndex < 0 || static_cast<int64_t>(spendTx.vout.size()) - 1 < txIndex )
            throw JSONRPCError(RPC_INVALID_PARAMETER, "<index> out of range");

        const CTxOut &utxo = spendTx.vout[txIndex];

        // **
        // Validate the given address
        // **

        CSmartAddress voteAddress(params[1].get_str());

        if ( !voteAddress.IsValid() )
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");

        CKeyID voteAddressKeyID;
        CKey vaKey;

        if (!voteAddress.GetKeyID(voteAddressKeyID))
            throw JSONRPCError(RPC_TYPE_ERROR, "<address> doesn't refer to key");

        if( GetVoteKeyForAddress(voteAddress, voteKey) ){
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Address is already registered for key: %s", voteKey.ToString()));
        }

        // If there is already a registration hash set for this address
        // Happens if the registration is sent but not confirmed and registered
        if( !pwalletMain->mapVotingKeyRegistrations[voteAddressKeyID].IsNull() ){
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Address has already a registration transaction assigned: %s",
                                                             pwalletMain->mapVotingKeyRegistrations[voteAddressKeyID].ToString()));
        }

        // If the given utxo is from the address to register use register option 1
        // Option 1 - verify the vote address with the input of the register tx
        // Option 2 - use a second signature in the op_return to verify the the vote address
        if( utxo.scriptPubKey == voteAddress.GetScript() )
            cRegisterOption = 0x01;
        else
            cRegisterOption = 0x02;

        // **
        // Get the private key of the vote address
        // **

        if( !pwalletMain->GetKey(voteAddressKeyID, vaKey) )
                throw JSONRPCError(RPC_WALLET_ERROR, "Private key for <address> not available");

        // **
        // Generate a new voting key
        // **

        CKey secret;
        secret.MakeNewKey(false);
        CVoteKeySecret voteKeySecret(secret);

        CKey vkKey = voteKeySecret.GetKey();

        if (!vkKey.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Voting private key outside allowed range");

        if( pwalletMain->HaveVotingKey(voteKeySecret.GetKey().GetPubKey().GetID()) )
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("VoteKey secret exists already in the voting storage %s", voteKeySecret.ToString()));

        CPubKey pubkey = vkKey.GetPubKey();

        if(!vkKey.VerifyPubKey(pubkey))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey verification failed");

        CKeyID vkKeyId = pubkey.GetID();
        voteKey.Set(vkKeyId);

        if( !voteKey.IsValid() )
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "VoteKey invalid");

        // Create the message to sign with the vote key and also voteaddress if required
        CDataStream ss(SER_GETHASH, 0);
        ss << strMessageMagic;
        ss << voteKey;
        ss << voteAddress;

        std::vector<unsigned char> vecSigAddress, vecSigVotekey;

        // Create the signature with the voting key
        if (!vkKey.SignCompact(Hash(ss.begin(), ss.end()), vecSigVotekey))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Signing with votekey failed");

        // And if required with the vote address
        if( cRegisterOption == 0x02 && !vaKey.SignCompact(Hash(ss.begin(), ss.end()), vecSigAddress))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Signing with votekey failed");

        std::vector<unsigned char> vecData = {
            OP_RETURN_VOTE_KEY_REG_FLAG,
            cRegisterOption
        };

        CDataStream registerData(SER_NETWORK,0);

        registerData << voteKey;
        registerData << vecSigVotekey;

        if( cRegisterOption == 0x02 ){
            registerData << voteAddress;
            registerData << vecSigAddress;
        }

        vecData.insert(vecData.end(), registerData.begin(), registerData.end());

        CScript registerScript = CScript() << OP_RETURN << vecData;

        // **
        // Create the transaction
        // **

        CCoinControl coinControl;
        COutPoint output(txHash, txIndex);

        CTxDestination change;

        if( cRegisterOption == 0x01 ){
            change = voteAddress.Get();
        }else{

            std::vector<CTxDestination> addresses;
            txnouttype type;
            int nRequired;

            if (!ExtractDestinations(utxo.scriptPubKey, type, addresses, nRequired) || addresses.size() != 1)
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't extract input address");


            change = addresses[0];
        }

        coinControl.fUseInstantSend = false;
        coinControl.Select(output);
        coinControl.destChange = change;

        // Create and send the transaction
        CWalletTx registerTx;
        CReserveKey reservekey(pwalletMain);
        CAmount nFeeRequired;
        std::string strError;
        vector<CRecipient> vecSend;
        int nChangePosRet = -1;

        CRecipient recipient = {registerScript, VOTEKEY_REGISTER_FEE, false};
        vecSend.push_back(recipient);

        if (!pwalletMain->CreateTransaction(vecSend, registerTx, reservekey, nFeeRequired, nChangePosRet,
                                             strError, &coinControl))
            throw JSONRPCError(RPC_WALLET_ERROR, strError);

        CValidationState state;
        if (!(CheckTransaction(registerTx, state, registerTx.GetHash(), false) || !state.IsValid()))
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("The registration transaction is invalid: %s", state.GetRejectReason()));

        VoteKeyParseResult parseResult = CheckVoteKeyRegistration(registerTx);
        if ( parseResult != VoteKeyParseResult::Valid )
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Registration transaction is invalid: %d", parseResult));

        if( !pwalletMain->AddVotingKeyPubKey(voteKeySecret.GetKey(), voteKeySecret.GetKey().GetPubKey()) )
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Failed to import VoteKey secret %s", voteKeySecret.ToString()));

        pwalletMain->mapVotingKeyRegistrations[voteAddressKeyID] = registerTx.GetHash();
        pwalletMain->mapVotingKeyMetadata[voteKeySecret.GetKey().GetPubKey().GetID()].registrationTxHash = registerTx.GetHash();

        pwalletMain->UpdateKeyMetadata(vaKey.GetPubKey());
        pwalletMain->UpdateVotingKeyMetadata(voteKeySecret.GetKey().GetPubKey().GetID());

        if (!pwalletMain->CommitTransaction(registerTx, reservekey, g_connman.get()))
            throw JSONRPCError(RPC_WALLET_ERROR, "The transaction was rejected!");

        UniValue result(UniValue::VOBJ);

        UniValue objTx(UniValue::VOBJ);
        TxToJSON(registerTx, uint256(), objTx);
        result.pushKV("registerTx", objTx);
        result.pushKV("voteAddress",voteAddress.ToString());
        result.pushKV("voteKey",voteKey.ToString());
        result.pushKV("voteKeySecret", voteKeySecret.ToString());

        return result;
    }

    if(strCommand == "get")
    {
        if (params.size() != 2)  {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'votekeys get <votekey/voteaddress>");
        }

        CVoteKey voteKey(params[1].get_str());
        CVoteKeyValue voteKeyValue;
        CSmartAddress voteAddress(params[1].get_str());

        if( !voteKey.IsValid() && !voteAddress.IsValid() )
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Parameter %s is neither a votekey nor a smartcash address", params[0].get_str()));

        if( voteAddress.IsValid() && !GetVoteKeyForAddress(voteAddress, voteKey) )
                throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("No votekey found for address %s", voteAddress.ToString()));

        if( voteKey.IsValid() && !GetVoteKeyValue(voteKey, voteKeyValue) )
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("No votekey value entry found for votekey %s", voteKey.ToString()));

        UniValue result(UniValue::VOBJ);

        result.pushKV("voteKey",voteKey.ToString());
        result.pushKV("voteAddress",voteKeyValue.voteAddress.ToString());
        result.pushKV("registerTx",voteKeyValue.nTxHash.ToString());
        result.pushKV("registerHeight", voteKeyValue.nBlockHeight);

        return result;
    }

    if(strCommand == "count")
    {
        std::vector<std::pair<CVoteKey,CVoteKeyValue>> vecVoteKeys;
        if( !GetVoteKeys(vecVoteKeys) )
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to load vote keys");

        return static_cast<int64_t>(vecVoteKeys.size());
    }

    if(strCommand == "list")
    {
        std::vector<std::pair<CVoteKey,CVoteKeyValue>> vecVoteKeys;
        if( !GetVoteKeys(vecVoteKeys) )
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to load vote keys");

        UniValue result(UniValue::VOBJ);

        for( auto vk : vecVoteKeys ){
            UniValue obj(UniValue::VOBJ);

            obj.pushKV("voteAddress",vk.second.voteAddress.ToString());
            obj.pushKV("registerTx",vk.second.nTxHash.ToString());
            obj.pushKV("registerHeight", vk.second.nBlockHeight);

            result.pushKV(vk.first.ToString(), obj);
        }

        return result;
    }
    if(strCommand == "encrypt")
    {
        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        if (!pwalletMain->IsVotingCrypted() && params.size() != 2)
            throw runtime_error(
                "votekeys encrypt \"passphrase\"\n"
                "\nEncrypts the voting storage with 'passphrase'. This is for first time encryption.\n"
                "If the voting storage is already encrypted, use the \"votekeys unlock\" command.\n"
                "Note that this will shutdown the server.\n"
                "\nArguments:\n"
                "1. \"passphrase\"    (string) The pass phrase to encrypt the voting storage with. It must be at least 1 character, but should be long.\n"
                "\nExamples:\n"
                "\nEncrypt you votekey storage\n"
                + HelpExampleCli("votekeys", "encrypt \"my pass phrase\"") +
                "\nNow set the passphrase to unlock the voting storage and use the voting features.\n"
                + HelpExampleCli("votekeys", "unlock \"my pass phrase\"") +
                "\nTo lock the voting storage again by removing the passphrase\n"
                + HelpExampleCli("votekeys", "lock") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("votekeys", "encrypt \"my pass phrase\"")
            );

        LOCK2(cs_main, pwalletMain->cs_wallet);

        if (pwalletMain->IsVotingCrypted())
            throw JSONRPCError(RPC_VOTEKEYS_WRONG_ENC_STATE, "Error: running with an encrypted voting storage, but encrypt was called.");

        // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
        // Alternately, find a way to make params[0] mlock()'d to begin with.
        SecureString strWalletPass;
        strWalletPass.reserve(100);
        strWalletPass = params[1].get_str().c_str();

        if (strWalletPass.length() < 1)
            throw runtime_error(
                "votekeys encrypt <passphrase>\n"
                "Encrypts the voting storage with <passphrase>.");

        if (!pwalletMain->EncryptVoting(strWalletPass))
            throw JSONRPCError(RPC_VOTEKEYS_ENCRYPTION_FAILED, "Error: Failed to encrypt the voting storage.");

        // BDB seems to have a bad habit of writing old data into
        // slack space in .dat files; that is bad if the old data is
        // unencrypted private keys. So:
        StartShutdown();
        return "voting encrypted; SmartCash server stopping, restart to run with encrypted voting storage. You need to make a new backup.";
    }

    if(strCommand == "changepassphrase")
    {
        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        if (pwalletMain->IsVotingCrypted() &&  params.size() != 3)
            throw runtime_error(
                "votekeys changepassphrase \"oldpassphrase\" \"newpassphrase\"\n"
                "\nChanges the voting passphrase from 'oldpassphrase' to 'newpassphrase'.\n"
                "\nArguments:\n"
                "1. \"oldpassphrase\"      (string) The current passphrase\n"
                "2. \"newpassphrase\"      (string) The new passphrase\n"
                "\nExamples:\n"
                + HelpExampleCli("votekeys", "changepassphrase \"old one\" \"new one\"")
                + HelpExampleRpc("votekeys", "changepassphrase \"old one\", \"new one\"")
            );

        LOCK2(cs_main, pwalletMain->cs_wallet);

        if (fHelp)
            return true;
        if (!pwalletMain->IsVotingCrypted())
            throw JSONRPCError(RPC_VOTEKEYS_WRONG_ENC_STATE, "Error: running with an unencrypted voting storage, but changepassphrase was called.");

        // TODO: get rid of these .c_str() calls by implementing SecureString::operator=(std::string)
        // Alternately, find a way to make params[0] mlock()'d to begin with.
        SecureString strOldWalletPass;
        strOldWalletPass.reserve(100);
        strOldWalletPass = params[1].get_str().c_str();

        SecureString strNewWalletPass;
        strNewWalletPass.reserve(100);
        strNewWalletPass = params[2].get_str().c_str();

        if (strOldWalletPass.length() < 1 || strNewWalletPass.length() < 1)
            throw runtime_error(
                "votekeys changepassphrase <oldpassphrase> <newpassphrase>\n"
                "Changes the voting storages passphrase from <oldpassphrase> to <newpassphrase>.");

        if (!pwalletMain->ChangeVotingPassphrase(strOldWalletPass, strNewWalletPass))
            throw JSONRPCError(RPC_VOTEKEYS_PASSPHRASE_INCORRECT, "Error: The voting passphrase entered was incorrect.");

        return NullUniValue;
    }

    if(strCommand == "unlock")
    {
        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        if (pwalletMain->IsVotingCrypted() && params.size() != 3)
            throw runtime_error(
                "votekeys unlock \"passphrase\" timeout\n"
                "\nStores the voting decryption key in memory for 'timeout' seconds.\n"
                "This is needed prior to performing actions related to voting keys\n"
                "\nArguments:\n"
                "1. \"passphrase\"     (string, required) The voting passphrase\n"
                "2. timeout            (numeric, required) The time to keep the decryption key in seconds.\n"
                "\nNote:\n"
                "Issuing the unlock command while the voting is already unlocked will set a new unlock\n"
                "time that overrides the old one.\n"
                "\nExamples:\n"
                "\nunlock voting for 60 seconds\n"
                + HelpExampleCli("votekeys", "unlock \"my pass phrase\" 60") +
                "\nLock the voting again (before 60 seconds)\n"
                + HelpExampleCli("votekeys", "lock") +
                "\nAs json rpc call\n"
                + HelpExampleRpc("votekeys", "unlock \"my pass phrase\", 60")
            );

        LOCK2(cs_main, pwalletMain->cs_wallet);

        if (!pwalletMain->IsVotingCrypted())
            throw JSONRPCError(RPC_VOTEKEYS_WRONG_ENC_STATE, "Error: running with an unencrypted voting storage, but unlock was called.");

        // Note that the walletpassphrase is stored in params[1] which is not mlock()ed
        SecureString strWalletPass;
        strWalletPass.reserve(100);
        // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
        // Alternately, find a way to make params[1] mlock()'d to begin with.
        strWalletPass = params[1].get_str().c_str();

        if (strWalletPass.length() > 0)
        {
            if (!pwalletMain->UnlockVoting(strWalletPass))
                throw JSONRPCError(RPC_VOTEKEYS_PASSPHRASE_INCORRECT, "Error: The voting passphrase entered was incorrect.");
        }
        else
            throw runtime_error(
                "votekeys unlock <passphrase> <timeout>\n"
                "Stores the voting decryption key in memory for <timeout> seconds.");

        int64_t nSleepTime = ParseJSON(params[2].get_str()).get_int64();
        LOCK(cs_nVotingUnlockTime);
        nVotingUnlockTime = GetTime() + nSleepTime;
        RPCRunLater("lockvoting", boost::bind(LockVoting, pwalletMain), nSleepTime);

        return NullUniValue;
    }

    if(strCommand == "lock")
    {
        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        if (pwalletMain->IsVotingCrypted() && params.size() != 1)
            throw runtime_error(
                "votekeys lock\n"
                "\nRemoves the voting encryption key from memory, locking the voting storage.\n"
                "After calling this method, you will need to call \"votekeys unlock\" again\n"
                "before being able to call any methods which require the voting to be unlocked.\n"
                "\nExamples:\n"
                + HelpExampleCli("votekeys", "lock")
                + HelpExampleRpc("votekeys", "lock")
            );

        LOCK2(cs_main, pwalletMain->cs_wallet);

        if (!pwalletMain->IsVotingCrypted())
            throw JSONRPCError(RPC_VOTEKEYS_WRONG_ENC_STATE, "Error: running with an unencrypted voting storage, but walletlock was called.");

        {
            LOCK(cs_nVotingUnlockTime);
            pwalletMain->LockVoting();
            nWalletUnlockTime = 0;
        }

        return NullUniValue;
    }

    if(strCommand == "import")
    {
        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        if(params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'votekeys import <vote-key-secret>'");

        EnsureVotingIsUnlocked();

        CVoteKeySecret voteKeySecret;

        if( !voteKeySecret.SetString(params[1].get_str()) )
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Invalid <vote-key-secret>: %s", params[1].get_str()));

        CPubKey pubKey = voteKeySecret.GetKey().GetPubKey();
        CVoteKey voteKey(pubKey.GetID());

        LOCK(pwalletMain->cs_wallet);

        if( !voteKey.IsValid() )
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Invalid voteKey public: %s", voteKey.ToString()));

        if( pwalletMain->HaveVotingKey(pubKey.GetID()) )
            throw JSONRPCError(RPC_INTERNAL_ERROR, "VoteKey secret exists already in the voting storage");

        if( !pwalletMain->AddVotingKeyPubKey(voteKeySecret.GetKey(), pubKey) )
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to import votekey-secret");

        if( !pwalletMain->HaveVotingKey(pubKey.GetID()) )
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("VoteKey %s is not available in the voting storage", voteKey.ToString()));

        CVotingKeyMetadata meta = pwalletMain->mapVotingKeyMetadata[pubKey.GetID()];
        meta.fImported = true;

        if( !pwalletMain->UpdateVotingKeyMetadata(pubKey.GetID()) )
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to update the VoteKey metadata");

         UniValue result(UniValue::VOBJ);

         result.pushKV("imported", voteKey.ToString());

         return result;
    }

    if(strCommand == "available")
    {
        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        if(params.size() != 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'votekeys available'");

        LOCK(pwalletMain->cs_wallet);

        std::set<CKeyID> setVoteKeyIds;

        pwalletMain->GetVotingKeys(setVoteKeyIds);

        UniValue result(UniValue::VARR);

        for( auto keyId : setVoteKeyIds ){

            UniValue obj(UniValue::VOBJ);

            CVoteKey voteKey(keyId);

            CVoteKeyValue voteKeyValue;
            std::string strVoteAddress;
            if(GetVoteKeyValue(voteKey, voteKeyValue)){
                strVoteAddress = voteKeyValue.voteAddress.ToString();
            }else{
                strVoteAddress = "Not registered";
            }

            obj.pushKV("voteKey", voteKey.ToString());
            obj.pushKV("voteAddress", strVoteAddress);
            obj.pushKV("enabled", pwalletMain->mapVotingKeyMetadata[keyId].fEnabled);

            result.push_back(obj);
        }

        return result;
    }

    if(strCommand == "update")
    {
        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        if(params.size() != 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'votekeys update <vote-key> <true/false (enabled/disabled)>'");

        CVoteKey voteKey(params[1].get_str());
        bool fEnabled = ParseJSON(params[2].get_str()).get_bool();

        CKeyID keyId;
        if( !voteKey.GetKeyID(keyId) )
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid <vote-key>: %s'",params[1].get_str()));

        LOCK(pwalletMain->cs_wallet);

        if( !pwalletMain->HaveVotingKey(keyId) )
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("VoteKey %s is not available in the voting storage", voteKey.ToString()));

        pwalletMain->mapVotingKeyMetadata[keyId].fEnabled = fEnabled;

        if( !pwalletMain->UpdateVotingKeyMetadata(keyId) )
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to update the VoteKey");

        UniValue obj(UniValue::VOBJ);

        CVoteKeyValue voteKeyValue;
        std::string strVoteAddress;
        if(GetVoteKeyValue(voteKey, voteKeyValue)){
            strVoteAddress = voteKeyValue.voteAddress.ToString();
        }else{
            strVoteAddress = "Not registered";
        }

        obj.pushKV("voteKey", voteKey.ToString());
        obj.pushKV("voteAddress", strVoteAddress);
        obj.pushKV("enabled", pwalletMain->mapVotingKeyMetadata[keyId].fEnabled);

        return obj;
    }

    if(strCommand == "export")
    {
        if( !pwalletMain )
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available.");

        EnsureVotingIsUnlocked();

        std::set<CKeyID> setVoteKeyIds;

        pwalletMain->GetVotingKeys(setVoteKeyIds);

        UniValue result(UniValue::VARR);
        for( auto keyId : setVoteKeyIds ){
            UniValue obj(UniValue::VOBJ);

            obj.pushKV("voteKey", CVoteKey(keyId).ToString());

            CKey secret;
            if( pwalletMain->GetVotingKey(keyId, secret) ){
                CVoteKeySecret voteKeySecret(secret);
                obj.pushKV("voteKeySecret", voteKeySecret.ToString());
            }else{
                obj.pushKV("voteKeySecret", "Failed to export");
            }

            result.push_back(obj);
        }

        return result;
    }

    return NullUniValue;
}
