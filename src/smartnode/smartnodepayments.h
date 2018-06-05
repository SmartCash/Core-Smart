// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTNODE_PAYMENTS_H
#define SMARTNODE_PAYMENTS_H

#include "../util.h"
#include "../core_io.h"
#include "../key.h"
#include "../net_processing.h"
#include "smartnode.h"
#include "../utilstrencodings.h"

class CSmartnodePayments;
class CSmartnodePaymentVote;
class CSmartnodeBlockPayees;

static const int MNPAYMENTS_SIGNATURES_REQUIRED         = 6;
static const int MNPAYMENTS_SIGNATURES_TOTAL            = 10;
static const int MNPAYMENTS_NO_RANK                    = INT_MAX;

//! minimum peer version that can receive and send smartnode payment messages,
//  vote for smartnode and be elected as a payment winner
// V1 - Last protocol version before update
// V2 - Newest protocol version
static const int MIN_SMARTNODE_PAYMENT_PROTO_VERSION_1 = 90025;
static const int MIN_SMARTNODE_PAYMENT_PROTO_VERSION_2 = 90026;

extern CCriticalSection cs_vecPayees;
extern CCriticalSection cs_mapSmartnodeBlocks;
extern CCriticalSection cs_mapSmartnodePayeeVotes;

extern CSmartnodePayments mnpayments;

namespace SmartNodePayments{

CAmount Payment(int nHeight);
int PayoutInterval(int nHeight);
int PayoutsPerBlock(int nHeight);

bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet);
bool IsPaymentValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward, CAmount& nodeReward);
void FillPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartNodes);
std::string GetRequiredPaymentsString(int nBlockHeight);

}

struct CSmartNodeWinners : public std::vector<smartnode_info_t>{};

struct CScriptVector : public std::vector<CScript>
{

    std::string ToString() const{

        std::ostringstream info;

        BOOST_FOREACH(CScript scriptPubKey, *this)
        {
            info << ", " << ScriptToAsmStr(scriptPubKey);
        }
        return info.str();
    }
};

class CSmartnodePayee
{
private:
    CScript scriptPubKey;
    std::vector<uint256> vecVoteHashes;

public:
    CSmartnodePayee() :
        scriptPubKey(),
        vecVoteHashes()
        {}

    CSmartnodePayee(CScript payee, uint256 hashIn) :
        scriptPubKey(payee),
        vecVoteHashes()
    {
        vecVoteHashes.push_back(hashIn);
    }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(vecVoteHashes);
    }

    CScript GetPayee() { return scriptPubKey; }

    void AddVoteHash(uint256 hashIn) { vecVoteHashes.push_back(hashIn); }
    std::vector<uint256> GetVoteHashes() { return vecVoteHashes; }
    int GetVoteCount() const { return vecVoteHashes.size(); }
};

// Keep track of votes for payees from smartnodes
class CSmartnodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CSmartnodePayee> vecPayees;

    CSmartnodeBlockPayees() :
        nBlockHeight(0),
        vecPayees()
        {}
    CSmartnodeBlockPayees(int nBlockHeightIn) :
        nBlockHeight(nBlockHeightIn),
        vecPayees()
        {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayees);
    }

    void AddPayees(const CSmartnodePaymentVote& vote);
    bool GetBestPayees(CScriptVector& payeeRet);
    bool HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq);

    bool IsTransactionValid(const CTransaction& txNew, CAmount expectedNodeReward);

    std::string GetRequiredPaymentsString();
};

// vote for the winning payment
class CSmartnodePaymentVote
{
public:
    CTxIn vinSmartnode;

    int nBlockHeight;
    CScriptVector payees;
    std::vector<unsigned char> vchSig;

    CSmartnodePaymentVote() :
        vinSmartnode(),
        nBlockHeight(0),
        payees(),
        vchSig()
        {}

    CSmartnodePaymentVote(COutPoint outpointSmartnode, int nBlockHeight, CScriptVector& payees) :
        vinSmartnode(outpointSmartnode),
        nBlockHeight(nBlockHeight),
        payees(payees),
        vchSig()
        {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        std::vector<CScriptBase> bases;
        READWRITE(vinSmartnode);
        READWRITE(nBlockHeight);
        READWRITE(vchSig);

        if (ser_action.ForRead()) {
            READWRITE(bases);
            BOOST_FOREACH(const CScriptBase& scriptBase, bases){
                payees.push_back(CScript(scriptBase.begin(),scriptBase.end()));
            }
        }else{
            BOOST_FOREACH(const CScript& scriptPubKey, payees){
                bases.push_back(*(CScriptBase*)(&scriptPubKey));
            }
            READWRITE(bases);
        }
    }

    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        BOOST_FOREACH(const CScript& scriptPubKey, payees)
        {
            ss << (*(CScriptBase*)(&scriptPubKey));
        }
        ss << nBlockHeight;
        ss << vinSmartnode.prevout;
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeySmartnode, int nValidationHeight, int &nDos);

    bool IsValid(CNode* pnode, int nValidationHeight, std::string& strError, CConnman& connman);
    void Relay(CConnman& connman);

    bool IsVerified() { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    std::string ToString() const;
};

//
// Smartnode Payments Class
// Keeps track of who should get paid for which blocks
//

class CSmartnodePayments
{
private:
    // smartnode count times nStorageCoeff payments blocks should be stored ...
    const float nStorageCoeff;
    // ... but at least nMinBlocksToStore (payments blocks)
    const int nMinBlocksToStore;

    // Keep track of current block height
    int nCachedBlockHeight;

public:
    std::map<uint256, CSmartnodePaymentVote> mapSmartnodePaymentVotes;
    std::map<int, CSmartnodeBlockPayees> mapSmartnodeBlocks;
    std::map<COutPoint, int> mapSmartnodesLastVote;
    std::map<COutPoint, int> mapSmartnodesDidNotVote;

    CSmartnodePayments() : nStorageCoeff(1.25), nMinBlocksToStore(5000) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapSmartnodePaymentVotes);
        READWRITE(mapSmartnodeBlocks);
    }

    void Clear();

    bool AddPaymentVote(const CSmartnodePaymentVote& vote);
    bool HasVerifiedPaymentVote(uint256 hashIn);
    bool ProcessBlock(int nBlockHeight, CConnman& connman);
    void CheckPreviousBlockVotes(int nPrevBlockHeight);

    void Sync(CNode* node, CConnman& connman);
    void RequestLowDataPaymentBlocks(CNode* pnode, CConnman& connman);
    void CheckAndRemove();

    bool GetBlockPayees(int nBlockHeight, CScriptVector& payees);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight, CAmount expectedNodeReward);
    bool IsScheduled(CSmartnode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outSmartnode, int nBlockHeight);

    int GetMinSmartnodePaymentsProto();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartNodes);
    std::string ToString() const;

    int GetBlockCount() { return mapSmartnodeBlocks.size(); }
    int GetVoteCount() { return mapSmartnodePaymentVotes.size(); }

    bool IsEnoughData();
    int GetStorageLimit();

    void UpdatedBlockTip(const CBlockIndex *pindex, CConnman& connman);
};

#endif
