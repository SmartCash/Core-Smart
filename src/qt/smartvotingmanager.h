// Copyright (c) 2017-2018 The SmartCash Core developers
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
};

class SmartProposalVote{

    int proposalId;
    std::string voteType;
    std::map<std::string, std::string> mapSignatures;
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

    void AddVote(std::string address, std::string message);

    int GetProposalId() const {return proposalId;}
    CAmount GetVotingPower() const {return nVotingPower;}
    std::string GetVoteType() const {return voteType;}
    std::string ToJson() const;
};

class SmartVotingCache
{
private:
    std::map<int, SmartProposalVote> mapVoted;

public:
    SmartVotingCache(){}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapVoted);
    }

    void AddVote(const SmartProposalVote &vote){mapVoted[vote.GetProposalId()] = vote;}
    bool HasVote(int proposalId) {return mapVoted.count(proposalId) > 0;}
    SmartProposalVote GetVote(int proposalId) {return mapVoted[proposalId];}
    std::string ToString() const{return strprintf("SmartVotingCache(stored votes: %d)",mapVoted.size());}

    // Dummies..for the flatDB.
    void CheckAndRemove(){}
    void Clear(){}
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

    QNetworkCookie * session;

    void CreateVotes(const std::map<SmartProposal, SmartHiveVoting::Type> &mapProposals, const std::vector<std::string> &vecAddresses, CAmount nVotingPower, std::map<SmartProposalVote, std::string> &mapVotes);
    void CastVote(const SmartProposalVote &vote);
    void UpdateProposals();
    void SyncCache();
    const std::vector<SmartProposal *> &GetProposals();
    SmartVotingCache& Cache(){return votingCache;}

private:

    QNetworkAccessManager * networkManager;
    QHash<QNetworkReply * ,SmartHiveRequest*> replies;
    std::vector<SmartProposal*> vecProposals;
    SmartVotingCache votingCache;

private Q_SLOTS:
    void replyFinished(QNetworkReply* reply);

Q_SIGNALS:
    void proposalsUpdated(const std::string &strErr);
    void voted(const SmartProposalVote &vote, const QJsonArray &result, const std::string &strErr);
};

#endif // SMARTVOTINGMANAGER_H
