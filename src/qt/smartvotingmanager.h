// Copyright (c) 2017-2019 The SmartCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTVOTINGMANAGER_H
#define SMARTVOTINGMANAGER_H

#include <QMap>
#include <QtNetwork>
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkCookie>
#include <QJsonDocument>
#include <QJsonObject>

#include "amount.h"
#include "util.h"
#include "serialize.h"
#include "sync.h"

class WalletModel;
class SmartVotingAddress;

namespace SmartHiveVoting {

enum Requests{
    GetProposals,
    CastVote
};

enum Result{
    Success,
    NoValidJson,
    AlreadyLoggedIn,
    InvalidUrl,
    ConnectionError,
    NoConnection,
    Unknown
};

enum Type{
    Yes,
    No,
    Abstain,
    Disabled
};

}

class SmartVotingAddress
{
    QString address;
    CAmount nAmount;
    bool fEnabled;

public:
    SmartVotingAddress(const std::string &address, const CAmount nAmount, const bool fEnabled = true):
        address(QString::fromStdString(address)), nAmount(nAmount), fEnabled(fEnabled){}

    friend bool operator==(const SmartVotingAddress& a, const SmartVotingAddress& b)
    {
        return (a.address == b.address);
    }

    friend bool operator!=(const SmartVotingAddress& a, const SmartVotingAddress& b)
    {
        return !(a == b);
    }

    friend bool operator<(const SmartVotingAddress& a, const SmartVotingAddress& b)
    {
        return a.address < b.address;
    }

    void SetEnabled(bool fState){fEnabled = fState;}
    void SetAmount(CAmount nAmount){this->nAmount = nAmount;}
    bool IsEnabled() const {return fEnabled;}
    QString GetAddress() const {return address;}
    double GetVotingPower() const {return nAmount / COIN + ( double(nAmount % COIN) / COIN );}
};

class SmartProposal{

    int proposalId;
    QString proposalKey;
    QString title;
    QString url;
    QString owner;
    double amountSmart;
    double amountUSD;
    QString votingDeadline;
    QString createdDate;
    double voteYes;
    double voteNo;
    double voteAbstain;
    double percentYes;
    double percentNo;
    double percentAbstain;
    std::vector <SmartVotingAddress> yesVotes;
    std::vector <SmartVotingAddress> noVotes;
    std::vector <SmartVotingAddress> abstainVotes;

public:

    SmartProposal(){}

    static SmartProposal * fromJsonObject(QJsonObject &object);

    friend bool operator==(const SmartProposal& a, const SmartProposal& b)
    {
        return (a.getProposalId() == b.getProposalId());
    }

    friend bool operator!=(const SmartProposal& a, const SmartProposal& b)
    {
        return !(a == b);
    }

    friend bool operator<(const SmartProposal& a, const SmartProposal& b)
    {
        return a.getProposalId() < b.getProposalId();
    }

    int getProposalId() const {return proposalId;}
    QString getProposalKey() const {return proposalKey;}
    QString getTitle() const {return title;}
    QString getUrl() const {return url;}
    QString getOwner() const {return owner;}
    double getAmountSmart() const {return amountSmart;}
    double getAmountUSD() const {return amountUSD;}
    QString getVotingDeadline() const {return votingDeadline;}
    QString getCreatedDate() const {return createdDate;}
    double getVoteYes() const {return voteYes;}
    double getVoteNo() const {return voteNo;}
    double getVoteAbstain() const {return voteAbstain;}
    double getPercentYes() const {return percentYes;}
    double getPercentNo() const {return percentNo;}
    double getPercentAbstain() const {return percentAbstain;}
    double getVotedAmount(SmartHiveVoting::Type type);
};

class SmartProposalVote{

    int proposalId;
    std::string voteType;
    std::map<SmartVotingAddress, std::string> mapSignatures;
    CAmount nVotingPower;

public:
    SmartProposalVote(){}
    SmartProposalVote(const SmartProposal &proposal, SmartHiveVoting::Type voteType, CAmount nVotingPower){
        this->proposalId = proposal.getProposalId();
        this->nVotingPower = nVotingPower;

        switch(voteType){
        case SmartHiveVoting::Yes:
            this->voteType = "YES";
            break;
        case SmartHiveVoting::No:
            this->voteType = "NO";
            break;
        case SmartHiveVoting::Abstain:
            this->voteType = "ABSTAIN";
            break;
        case SmartHiveVoting::Disabled:
            this->voteType = "error";
            break;
        }

        mapSignatures.clear();
    }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(proposalId);
        READWRITE(voteType);
        READWRITE(nVotingPower);
    }


    friend bool operator==(const SmartProposalVote& a, const SmartProposalVote& b)
    {
        return (a.proposalId == b.proposalId);
    }

    friend bool operator!=(const SmartProposalVote& a, const SmartProposalVote& b)
    {
        return !(a == b);
    }

    friend bool operator<(const SmartProposalVote& a, const SmartProposalVote& b)
    {
        return a.proposalId < b.proposalId;
    }

    void AddVote(const SmartVotingAddress &address, const std::string &message);
    int GetProposalId() const {return proposalId;}
    double GetVotingPower() const {
        double nVotingPower = 0;
        for( auto p:mapSignatures) nVotingPower += p.first.GetVotingPower();
        return nVotingPower;
    }
    std::string GetVoteType() const {return voteType;}
    QString ToJson() const;
};

class SmartHiveRequest: public QNetworkRequest
{

public:
    QString endpoint;
    SmartProposalVote vote;

    SmartHiveRequest();
    SmartHiveRequest(QString endpoint);
    SmartHiveRequest(QString endpoint, const SmartProposalVote &vote);
};

class SmartVotingManager: public QObject
{

    Q_OBJECT

public:

    SmartVotingManager();

    CCriticalSection cs_addresses;

    void setWalletModel(WalletModel *model);

    void CreateVotes(const std::map<SmartProposal, SmartHiveVoting::Type> &mapProposals, std::map<SmartProposalVote, std::string> &mapVotes);
    void CastVote(const SmartProposalVote &vote);
    void UpdateProposals();
    const std::vector<SmartProposal *> &GetProposals();
    std::vector<SmartVotingAddress> &GetAddresses(){return vecAddresses;}
    int GetEnabledAddressCount();
    double GetVotingPower();

private:

    WalletModel * walletModel;
    QNetworkAccessManager * networkManager;
    QHash<QNetworkReply * ,SmartHiveRequest*> replies;
    std::vector<SmartVotingAddress> vecAddresses;
    std::vector<SmartProposal*> vecProposals;

    void updateAddresses();

private Q_SLOTS:
    void replyFinished(QNetworkReply* reply);
    void balanceChanged(const CAmount &balance, const CAmount &unconfirmedBalance, const CAmount &immatureBalance, const CAmount &watchOnlyBalance, const CAmount &watchUnconfBalance, const CAmount &watchImmatureBalance);

Q_SIGNALS:
    void addressesUpdated();
    void proposalsUpdated(const std::string &strErr);
    void voted(const SmartProposalVote &vote, const QJsonArray &result, const std::string &strErr);
};

#endif // SMARTVOTINGMANAGER_H
