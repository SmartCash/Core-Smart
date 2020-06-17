// Copyright (c) 2017-2019 The SmartCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartvotingmanager.h"
#include "base58.h"
#include "messagesigner.h"
#include "util.h"
#include "validation.h"
#include "wallet/wallet.h"
#include "walletmodel.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSslConfiguration>

const QString urlHiveVotingPortal = "https://vote.smartcash.cc/api/v1/";
const QString urlHiveVotingPortalTestnet = "https://testnet-vote.smrt.cash/api/v1/";

SmartHiveRequest::SmartHiveRequest(QString endpoint):
    QNetworkRequest(),
    endpoint(endpoint)
{
    if(MainNet()){
        setUrl(QUrl(urlHiveVotingPortal + endpoint));
    }else{
        setUrl(QUrl(urlHiveVotingPortalTestnet + endpoint));
    }

    setHeader(QNetworkRequest::ContentTypeHeader,
                "application/json");
}

SmartHiveRequest::SmartHiveRequest(QString endpoint, const SmartProposalVote &vote):
    SmartHiveRequest(endpoint)
{
    this->vote = vote;
}

SmartVotingManager::SmartVotingManager(): walletModel(0), networkManager(0)
{
    networkManager = new QNetworkAccessManager;

    connect(networkManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(replyFinished(QNetworkReply*)));
}

void SmartVotingManager::CreateVotes(const std::map<SmartProposal, SmartHiveVoting::Type> &mapProposals, std::map<SmartProposalVote, std::string> &mapVotes)
{

    LOCK2(cs_main, pwalletMain->cs_wallet);

    mapVotes.clear();

    for( auto proposal : mapProposals ){

        SmartProposalVote vote(proposal.first, proposal.second, 0);
        std::string result = "";

        for( auto voteAddress : vecAddresses ){

            if( !voteAddress.IsEnabled() ) continue;

            std::string address = voteAddress.GetAddress().toStdString();

            CBitcoinAddress addr(address);
            if (!addr.IsValid()){
                std::string errStr = strprintf("Invalid address %s\n",address);
                result += errStr;
                LogPrint("smartvoting", "SmartVotingManager::Vote -- %s\n", errStr);
            }else{

                CKeyID keyID;
                if (!addr.GetKeyID(keyID)){
                    std::string errStr = strprintf("Address does not refer to key %s\n",address);
                    result += errStr;
                    LogPrint("smartvoting", "SmartVotingManager::Vote -- %s\n", errStr);
                }else{

                    CKey key;
                    if (!pwalletMain->GetKey(keyID, key)){
                        std::string errStr = strprintf("Private key not available for address %s\n",address);
                        result += errStr;
                        LogPrint("smartvoting", "SmartVotingManager::Vote -- %s\n", errStr);
                    }else{

                        std::vector<unsigned char> vchSig;

                        if (!CMessageSigner::SignMessage(proposal.first.getUrl().toStdString(),vchSig,key)){
                            std::string errStr = strprintf("Sign failed for address %s\n",address);
                            result += errStr;
                            LogPrint("smartvoting", "SmartVotingManager::Vote -- %s\n", errStr);
                        }else{

                            std::string votingMessage = EncodeBase64(&vchSig[0], vchSig.size());
                            vote.AddVote(voteAddress, votingMessage);
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
    qReply = networkManager->post(*castVote, vote.ToJson().toUtf8());

    if( qReply ){
        replies.insert(qReply,castVote);
    }
}

void SmartVotingManager::replyFinished(QNetworkReply* reply)
{

    if( !replies.contains(reply)  ){
        LogPrint("smartvoting", "SmartVotingManager::replyFinished -- unexpected request: %s\n", reply->request().url().toString().toStdString());
        return;
    }

    SmartHiveRequest *request = replies.take(reply);

    QString&& resultString = reply->readAll();

    LogPrint("smartvoting", "SmartVotingManager::replyFinished -- status: %d, result: %s\n",reply->error(), resultString.toStdString());

    std::string strErr = "";

    if( request->endpoint == "voteproposals/checkaddresses" ){
        LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- voteproposals/checkaddresses\n");

        QJsonObject obj = QJsonObject(QJsonDocument::fromJson(resultString.toUtf8()).object());

        if(reply->error() == QNetworkReply::NoError){

            if( !obj.contains("status") && !obj.contains("result") ){
                LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- voteproposals/checkaddresses: invalid response\n");
                strErr = "Invalid response received";
            }else{


                QString status = obj["status"].toString();

                if( status != "OK" ){
                    LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- voteproposals/checkaddresses: invalid status %s\n",status.toStdString());
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

            if(!obj.contains("ERROR")){
                strErr = strprintf("Request failed with %d",reply->error());
            }else{
                strErr = strprintf("Request failed - %s",obj["ERROR"].toString().toStdString());
            }

        }

        proposalsUpdated(strErr);

    }else if( request->endpoint == "VoteProposals/CastVoteList"){

        QJsonArray result;

        LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- VoteProposals/CastVoteList\n");

        QJsonObject obj = QJsonObject(QJsonDocument::fromJson(resultString.toUtf8()).object());

        if(reply->error() == QNetworkReply::NoError){

            if( !obj.contains("status") && !obj.contains("result") ){
                LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- VoteProposals/CastVoteList: invalid response\n");
                strErr = "Invalid response received";
            }else{

                QString status = obj["status"].toString();

                if( status != "OK" ){
                    LogPrint("smartvoting", "SmartVotingPage::hiveRequestDone -- VoteProposals/CastVoteList: invalid status %s\n",status.toStdString());
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
    SmartHiveRequest *getProposals = new SmartHiveRequest("voteproposals/checkaddresses");

    QJsonArray data;

    for( auto address : vecAddresses ){
        data.append( address.GetAddress() );
    }

    QNetworkReply * qReply = networkManager->post(*getProposals, QJsonDocument(data).toJson());

    if( qReply ){
        replies.insert(qReply,getProposals);
    }
}

const std::vector<SmartProposal *> &SmartVotingManager::GetProposals()
{
    return vecProposals;
}

int SmartVotingManager::GetEnabledAddressCount()
{
    int nCount = 0;

    for( auto address : vecAddresses ){
        if( address.IsEnabled() ) nCount++;
    }

    return nCount;
}

double SmartVotingManager::GetVotingPower()
{
    double nVotingPower = 0;

    for( auto address : vecAddresses ){
        if( address.IsEnabled() ) nVotingPower += address.GetVotingPower();
    }

    return nVotingPower;
}

void SmartVotingManager::setWalletModel(WalletModel *model)
{
    if( walletModel ) return;

    walletModel = model;

    connect(walletModel, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)),
            this,SLOT(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

    updateAddresses();
}


void SmartVotingManager::balanceChanged(const CAmount &balance, const CAmount &unconfirmedBalance, const CAmount &immatureBalance, const CAmount &watchOnlyBalance, const CAmount &watchUnconfBalance, const CAmount &watchImmatureBalance)
{
    updateAddresses();
}

void SmartVotingManager::updateAddresses()
{

    LOCK2(cs_main, pwalletMain->cs_wallet);

    LOCK(cs_addresses);

    map<CTxDestination, CAmount> balances = pwalletMain->GetAddressBalances();
    BOOST_FOREACH(set<CTxDestination> grouping, pwalletMain->GetAddressGroupings())
    {
        BOOST_FOREACH(CTxDestination destination, grouping)
        {

            std::string address = CBitcoinAddress(destination).ToString();

            std::vector<SmartVotingAddress>::iterator inVect = std::find_if(vecAddresses.begin(), vecAddresses.end(),
            [address](const SmartVotingAddress& voteAddress) -> bool{
                if( address == voteAddress.GetAddress().toStdString() ) return true;
                return false;
            });

            CAmount nAmount = balances[destination];
            bool fEligible = nAmount >= COIN;
            bool fKnown = inVect != vecAddresses.end();

            if( fKnown && !fEligible ){
                vecAddresses.erase(inVect);
            }else if( fKnown ){
                inVect->SetAmount(nAmount);
            }else if( fEligible ){
                SmartVotingAddress newAddress(address,nAmount);
                vecAddresses.push_back(newAddress);
            }

        }
    }

    addressesUpdated();
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
       !object.contains("percentAbstain") ||
       !object.contains("addressStates"))
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

    QJsonArray addressStates = object["addressStates"].toArray();

    for( auto state : addressStates ){

        QJsonObject s = state.toObject();

        LogPrint("smartvoting", "SmartProposal::fromJsonObject -- addressState %s\n", QJsonDocument(s).toJson().toStdString());

        if(!s.contains("address") ||
           !s.contains("amount") ||
           !s.contains("type") ||
           !s.contains("valid") )
            continue;

        QString address = s["address"].toString();
        double amount = s["amount"].toDouble();
        QString type = s["type"].toString();
        bool valid = s["valid"].toBool();

        LogPrint("smartvoting", "address field type %d\n",s["address"].type());
        LogPrint("smartvoting", "amount field type %d\n",s["amount"].type());
        LogPrint("smartvoting", "type field type %d\n",s["type"].type());
        LogPrint("smartvoting", "valid field type %d\n",s["valid"].type());

        SmartVotingAddress voteAddress(address.toStdString(),amount * COIN,valid);

        if( type == "YES"){
            proposal->yesVotes.push_back(voteAddress);
        }else if( type == "NO"){
            proposal->noVotes.push_back(voteAddress);
        }else if( type == "ABSTAIN"){
            proposal->abstainVotes.push_back(voteAddress);
        }

    }

    return proposal;
}

double SmartProposal::getVotedAmount(SmartHiveVoting::Type type)
{

    double nVotingPower = 0;

    if( type == SmartHiveVoting::Yes ){

        for( auto address : yesVotes ){
            if( address.IsEnabled() ) nVotingPower += address.GetVotingPower();
        }

    }else if( type == SmartHiveVoting::No ){

        for( auto address : noVotes ){
            if( address.IsEnabled() ) nVotingPower += address.GetVotingPower();
        }

    }else if( type == SmartHiveVoting::Abstain ){

        for( auto address : abstainVotes ){
            if( address.IsEnabled() ) nVotingPower += address.GetVotingPower();
        }

    }else{

        for( auto address : yesVotes ){
            if( !address.IsEnabled() ) nVotingPower += address.GetVotingPower();
        }

        for( auto address : noVotes ){
            if( !address.IsEnabled() ) nVotingPower += address.GetVotingPower();
        }

        for( auto address : abstainVotes ){
            if( !address.IsEnabled() ) nVotingPower += address.GetVotingPower();
        }

    }

    return nVotingPower;
}

void SmartProposalVote::AddVote(const SmartVotingAddress &address, const std::string &message)
{
    if( !mapSignatures.count(address)){
        mapSignatures.insert(make_pair(address,message));
        nVotingPower += address.GetVotingPower();
    }
}

QString SmartProposalVote::ToJson() const
{
    QJsonObject root;
    QJsonArray votes;

    root["proposalId"] = proposalId;

    for( auto signature : mapSignatures ){

        QJsonObject vote;

        vote["smartAddress"] = signature.first.GetAddress();
        vote["signature"] = QString::fromStdString(signature.second);
        vote["voteType"] = QString::fromStdString(voteType);

        votes.append(vote);
    }

    root["votes"] = votes;

    return QJsonDocument(root).toJson();
}
