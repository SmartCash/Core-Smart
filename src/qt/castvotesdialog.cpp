// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "castvotesdialog.h"
#include "ui_castvotesdialog.h"

#include "addresstablemodel.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "init.h"
#include "smartnode/smartnodeconfig.h"
#include "messagesigner.h"
#include "util.h"
#include "smartvotingmanager.h"

#include <regex>

#include <boost/foreach.hpp>

#include <QApplication>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QFlags>
#include <QIcon>
#include <QSettings>
#include <QString>
#include <QRegularExpression>


QString SuccessText(QString text)
{
    return QString("<b><font color=\"#09720e\">%1</font></b>").arg(text);
}

QString ErrorText(QString text)
{
    return QString("<b><font color=\"#ba2e12\">%1</font></b>").arg(text);
}


CastVotesDialog::CastVotesDialog(const PlatformStyle *platformStyle, SmartVotingManager *votingManager, WalletModel *model, QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint),
    ui(new Ui::CastVotesDialog),
    platformStyle(platformStyle),
    votingManager(votingManager),
    walletModel(model)
{
    ui->setupUi(this);

    waitTimer.setSingleShot(true);

    connect(ui->button, SIGNAL(clicked()), this, SLOT(close()));
    connect(votingManager, SIGNAL(voted(const SmartProposalVote&, const QJsonArray&, const std::string&)),
            this, SLOT(voted(const SmartProposalVote&, const QJsonArray&, const std::string&)));
    connect(&waitTimer, SIGNAL(timeout()),this, SLOT(waitForResponse()));

    this->setWindowTitle("SmartHive voting");
}

CastVotesDialog::~CastVotesDialog()
{
    delete ui;
}

int CastVotesDialog::exec()
{
    QTimer::singleShot(2000, this, SLOT(start()));
    return QDialog::exec();
}

void CastVotesDialog::close()
{
    //votingManager->cancelAll();
    done(QDialog::Accepted);
}

void CastVotesDialog::start()
{

    if( !walletModel){
        ui->results->append("<br><b>Wallet not available!</b>");
        ui->button->setText("Close");
        return;
    }

    std::map<SmartProposalVote, std::string> mapResults;

    vecVotes.clear();

    ui->results->append(QString("<br>Signing overall <b>%1</b> message%2 for <b>%3</b> proposal%4.<br>")
                        .arg(votingManager->GetEnabledAddressCount() * mapVotings.size())
                        .arg(votingManager->GetEnabledAddressCount() * mapVotings.size() > 1 ? "s" : "")
                        .arg(mapVotings.size())
                        .arg(mapVotings.size() > 1 ? "s" : ""));

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()){
            ui->results->append("<br><b>Signing failed!</b>");
            ui->button->setText("Close");
            return;
        }

        votingManager->CreateVotes(mapVotings, mapResults);
    }else{
        votingManager->CreateVotes(mapVotings, mapResults);
    }

    for( auto result : mapResults ){
        if( result.second == "" ){
            vecVotes.push_back(result.first);
        }else{
            ui->results->append(ErrorText("ERROR ") + QString::fromStdString(result.second));
        }
    }

    voteOne();
}

void CastVotesDialog::voteOne()
{

    if( vecVotes.size() ){
        SmartProposalVote vote = vecVotes.back();
        vecVotes.pop_back();
        votingManager->CastVote(vote);

        ui->results->append(QString("<br>Vote <b>%1</b> with <b>%2 SMART</b> for proposal <b>#%3</b><br>")
                            .arg(QString::fromStdString(vote.GetVoteType()))
                            .arg(std::round(vote.GetVotingPower()),0,'f',0)
                            .arg(vote.GetProposalId()));

        ui->results->append("Wait for response");
        waitTimer.start(1000);
        return;
    }

    ui->results->append("<br><b>Done!</b>");
    ui->button->setText("Close");
}

void CastVotesDialog::waitForResponse()
{
    ui->results->moveCursor(QTextCursor::End);
    ui->results->textCursor().insertText(".");
    ui->results->moveCursor(QTextCursor::End);
    waitTimer.start(1000);
}

void CastVotesDialog::voted(const SmartProposalVote &vote, const QJsonArray &results, const std::string &strErr)
{

    waitTimer.stop();

    if( strErr != ""){
        ui->results->append(ErrorText("ERROR") + QString(" for proposal #%1 -- %2").arg(vote.GetProposalId()).arg(QString::fromStdString(strErr)));
    }else{

        ui->results->append(QString("<br>Result for proposal <b>#%1</b>").arg(vote.GetProposalId()));

        for( auto result : results ){
            QJsonObject obj = result.toObject();

            QString status = obj["status"].toString();
            QString address = obj["smartAddress"].toString();
            double amount = obj["amount"].toDouble();

            QString resultString;

            if( status == "OK" ){
                resultString = SuccessText(status);
            }else{
                resultString = ErrorText(status);
            }

            ui->results->append(QString("  -> %1 | %2 SMART <b>%3<b>").arg(address).arg(std::round(amount),0,'f',0).arg(resultString));

            Q_EMIT votedForAddress(address, vote.GetProposalId(), status == "OK");
        }

    }

    voteOne();
}
