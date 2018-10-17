// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PROPOSAL_H
#define PROPOSAL_H

#include "amount.h"
#include "chain.h"
#include "coins.h"
#include "base58.h"
#include "smarthive/hive.h"

extern const CAmount nProposalFee;

extern const size_t nProposalTitleLengthMin;
extern const size_t nProposalTitleLengthMax;

extern const int64_t nProposalMilestoneDistanceMax;
extern const size_t nProposalMilestoneDescriptionLengthMn;
extern const size_t nProposalMilestoneDescriptionLengthMax;

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

    bool IsDescriptionValid(std::string &strError);

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

class CProposalVote
{
    int nBlockHeight;
    CSmartAddress voteAddress;
    int64_t nMapping;

public:

    uint256 GetHash() const;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(voteAddress);
        READWRITE(nMapping);
    }
};

class CProposal
{

    int64_t nTimeCreated;
    std::vector<CProposalVote> vecVotes;

protected:

    std::string title;
    std::string url;
    CSmartAddress address;
    std::vector<CProposalMilestone> vecMilestones;

    uint256 nFeeHash;

public:

    CProposal() { SetNull(); }

    void SetNull(){
        nTimeCreated = -1;
        title = std::string();
        url = std::string();
        address = CSmartAddress();
        vecMilestones.clear();
        vecVotes.clear();
    }

    static bool ValidateTitle(const std::string &strTitle, std::string &strError);
    static bool ValidateUrl(const std::string &strUrl, std::string &strError);

    void SetTitle(const std::string &strTitle) { title = strTitle; }
    std::string GetTitle() const { return title; }
    bool IsTitleValid(std::string &strError);

    void SetUrl(const std::string &strUrl) { url = strUrl; }
    std::string GetUrl() const { return url; }
    bool IsUrlValid(std::string &strError);

    void SetAddress(const CSmartAddress &address) { this->address = address; }
    bool IsAddressValid(std::string &strError);
    CSmartAddress GetAddress() const { return address; }

    bool IsMilestoneVectorValid(std::string &strError);

    uint256 GetFeeHash() const { return nFeeHash; }

    void SetTime(int64_t nTime) { nTimeCreated = nTime; }
    int64_t GetTime() const { return nTimeCreated; }

    std::vector<CProposalMilestone> GetMilestones() const { return vecMilestones; }

    bool IsValid(std::string &strError);

    uint256 GetHash() const;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(title);
        READWRITE(url);
        READWRITE(address);
        READWRITE(vecMilestones);
        READWRITE(nFeeHash);
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
