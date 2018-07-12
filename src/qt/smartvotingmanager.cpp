// Copyright (c) 2017-2018 The SmartCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartvotingmanager.h"
#include "base58.h"
#include "smartnode/flat-database.h"
#include "util.h"
#include "validation.h"
#include "wallet/wallet.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QStandardPaths>

CFlatDB<SmartVotingCache> flatdb;

const QString urlHiveVotingPortal = "https://vote.smartcash.cc/api/v1/";

SmartHiveRequest::SmartHiveRequest(QString endpoint):
    QNetworkRequest(),
    endpoint(endpoint)
{
    setUrl(QUrl(urlHiveVotingPortal + endpoint));
    setHeader(QNetworkRequest::ContentTypeHeader,
                "application/x-www-form-urlencoded");
}

SmartHiveRequest::SmartHiveRequest(QString endpoint, const SmartProposalVote &vote):
    SmartHiveRequest(endpoint)
{
    this->vote = vote;
    setHeader(QNetworkRequest::ContentTypeHeader,
                "application/json");
}

SmartVotingManager::SmartVotingManager()
{
    networkManager = new QNetworkAccessManager;

    connect(networkManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(replyFinished(QNetworkReply*)));

    flatdb = CFlatDB<SmartVotingCache>("vote.dat", "magicVotingCache");
    if(!flatdb.Load(votingCache)) {
        flatdb.Dump(votingCache);
    }

    LogPrintf("SmartVotingManager init -- %s", votingCache.ToString());
}

void SmartVotingManager::CreateVotes(const std::map<SmartProposal, SmartHiveVoting::Type> &mapProposals, const std::vector<std::string> &vecAddresses, CAmount nVotingPower, std::map<SmartProposalVote, std::string> &mapVotes)
{

    LOCK2(cs_main, pwalletMain->cs_wallet);

    mapVotes.clear();

    for( auto proposal : mapProposals ){

        SmartProposalVote vote(proposal.first, proposal.second, nVotingPower);
        std::string result = "";

        for( auto address : vecAddresses ){

            CBitcoinAddress addr(address);
            if (!addr.IsValid()){
                std::string errStr = strprintf("Invalid address %s\n",address);
                result += errStr;
                LogPrint("smartvoting", "SmartVotingManager::Vote -- %s", errStr);
            }else{

                CKeyID keyID;
                if (!addr.GetKeyID(keyID)){
                    std::string errStr = strprintf("Address does not refer to key %s\n",address);
                    result += errStr;
                    LogPrint("smartvoting", "SmartVotingManager::Vote -- %s", errStr);
                }else{

                    CKey key;
                    if (!pwalletMain->GetKey(keyID, key)){
                        std::string errStr = strprintf("Private key not available for address %s\n",address);
                        result += errStr;
                        LogPrint("smartvoting", "SmartVotingManager::Vote -- %s", errStr);
                    }else{

                        CDataStream ss(SER_GETHASH, 0);
                        ss << strMessageMagic;
                        ss << proposal.first.getUrl().toStdString();

                        std::vector<unsigned char> vchSig;
                        if (!key.SignCompact(Hash(ss.begin(), ss.end()), vchSig)){
                            std::string errStr = strprintf("Sign failed for address %s\n",address);
                            result += errStr;
                            LogPrint("smartvoting", "SmartVotingManager::Vote -- %s", errStr);
                        }else{

                            std::string votingMessage = EncodeBase64(&vchSig[0], vchSig.size());
                            vote.AddVote(address, votingMessage);
                        }
                    }
                }
            }
        }

        mapVotes.insert(make_pair(vote,result));
    }

}

void SmartVotingManager::CastVote(const SmartProposalVote &vote)
{
    QNetworkReply * qReply;

    SmartHiveRequest *castVote = new SmartHiveRequest("VoteProposals/CastVoteList", vote);
    qReply = networkManager->post(*castVote, QString::fromStdString(vote.ToJson()).toUtf8());

    if( qReply ){
        replies.insert(qReply,castVote);
    }
}

void SmartVotingManager::replyFinished(QNetworkReply* reply)
{

    if( !replies.contains(reply)  ){
        LogPrint("smartvoting", "SmartVotingManager::replyFinished -- unexpected request: %s", reply->request().url().toString().toStdString());
        return;
    }

    LogPrint("smartvoting", "SmartVotingManager::replyFinished -- status: %d",reply->error());

    SmartHiveRequest *request = replies.take(reply);

    QString&& resultString = reply->readAll();

    std::string strErr = "";

    if( request->endpoint == "voteproposals" ){
        LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- voteproposals");

        if(reply->error() == QNetworkReply::NoError){

            QJsonObject obj = QJsonObject(QJsonDocument::fromJson(resultString.toUtf8()).object());

            if( !obj.contains("status") && !obj.contains("result") ){
                LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- voteproposals: invalid response");
                strErr = "Invalid response received";
            }else{


                QString status = obj["status"].toString();

                if( status != "OK" ){
                    LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- voteproposals: invalid status %s",status.toStdString());
                    strErr = "Invalid response received";
                }else{

                    vecProposals.clear();

                    QJsonArray proposals = obj["result"].toArray();

                    for( auto p : proposals ){

                        QJsonObject obj = p.toObject();
                        SmartProposal *proposal = SmartProposal::fromJsonObject(obj);

                        if( proposal ) vecProposals.push_back(proposal);
                    }

                }

            }
        }else{
            strErr = "Request failed";
        }

        proposalsUpdated(strErr);

    }else if( request->endpoint == "VoteProposals/CastVoteList"){

        QJsonArray result;

        LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- VoteProposals/CastVoteList");

        QJsonObject obj = QJsonObject(QJsonDocument::fromJson(resultString.toUtf8()).object());

        if(reply->error() == QNetworkReply::NoError){

            if( !obj.contains("status") && !obj.contains("result") ){
                LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- VoteProposals/CastVoteList: invalid response");
                strErr = "Invalid response received";
            }else{

                QString status = obj["status"].toString();

                if( status != "OK" ){
                    LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- VoteProposals/CastVoteList: invalid status %s",status.toStdString());
                    strErr = strprintf("Vote request failed %s",status.toStdString());
                }else{

                    result = obj["result"].toArray();
                }
            }

        }else{

            if(!obj.contains("ERROR")){
                strErr = strprintf("Request failed with %d",reply->error());
            }else{
                strErr = strprintf("Request failed - %s",obj["ERROR"].toString().toStdString());
            }

        }

        voted(request->vote, result, strErr);
    }

    delete request;
}

void SmartVotingManager::UpdateProposals()
{
    SmartHiveRequest *getProposals = new SmartHiveRequest("voteproposals");

    QNetworkReply * qReply = networkManager->get(*getProposals);

    if( qReply ){
        replies.insert(qReply,getProposals);
    }
}

void SmartVotingManager::SyncCache()
{
    flatdb.Dump(votingCache);
}

const std::vector<SmartProposal *> &SmartVotingManager::GetProposals()
{
    return vecProposals;
}

SmartProposal * SmartProposal::fromJsonObject(QJsonObject &object){

    if(!object.contains("proposalId") ||
       !object.contains("proposalKey") ||
       !object.contains("title") ||
       !object.contains("url") ||
       !object.contains("owner") ||
       !object.contains("amountSmart") ||
       !object.contains("amountUSD") ||
       !object.contains("votingDeadline") ||
       !object.contains("createdDate") ||
       !object.contains("voteYes") ||
       !object.contains("voteNo") ||
       !object.contains("voteAbstain") ||
       !object.contains("percentYes") ||
       !object.contains("percentNo") ||
       !object.contains("percentAbstain"))
        return nullptr;

    SmartProposal * proposal = new SmartProposal();

    proposal->proposalId = object["proposalId"].toInt();
    proposal->proposalKey = object["proposalKey"].toString();
    proposal->title = object["title"].toString();
    proposal->url = object["url"].toString();
    proposal->owner = object["owner"].toString();
    proposal->amountSmart = object["amountSmart"].toDouble();
    proposal->amountUSD = object["amountUSD"].toDouble();
    proposal->votingDeadline = object["votingDeadline"].toString();
    proposal->createdDate = object["createdDate"].toString();
    proposal->voteYes = object["voteYes"].toDouble();
    proposal->voteNo = object["voteNo"].toDouble();
    proposal->voteAbstain = object["voteAbstain"].toDouble();
    proposal->percentYes = object["percentYes"].toDouble();
    proposal->percentNo = object["percentNo"].toDouble();
    proposal->percentAbstain = object["percentAbstain"].toDouble();

    return proposal;
}

void SmartProposalVote::AddVote(std::string address, std::string message)
{
    if( !mapSignatures.count(address)){
        mapSignatures.insert(make_pair(address,message));
    }
}

string SmartProposalVote::ToJson() const
{
    QJsonObject root;
    QJsonArray votes;

    root["proposalId"] = proposalId;

    for( auto signature : mapSignatures ){

        QJsonObject vote;

        vote["smartAddress"] = QString::fromStdString(signature.first);
        vote["signature"] = QString::fromStdString(signature.second);
        vote["voteType"] = QString::fromStdString(voteType);

        votes.append(vote);
    }

    root["votes"] = votes;

    return QJsonDocument(root).toJson().toStdString();
}
