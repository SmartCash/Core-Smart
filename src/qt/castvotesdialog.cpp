// Copyright (c) 2018 - The SmartCash Developers
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


CastVotesDialog::CastVotesDialog(const PlatformStyle *platformStyle, SmartVotingManager *votingManager, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CastVotesDialog),
    platformStyle(platformStyle),
    votingManager(votingManager)
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
    std::map<SmartProposalVote, std::string> mapResults;

    vecVotes.clear();

    ui->results->append(QString("<br>Signing overall <b>%1</b> message%2 for <b>%3</b> proposal%4.<br>")
                        .arg(vecAddresses.size() * mapVotings.size())
                        .arg(vecAddresses.size() * mapVotings.size() > 1 ? "s" : "")
                        .arg(mapVotings.size())
                        .arg(mapVotings.size() > 1 ? "s" : ""));

    votingManager->CreateVotes(mapVotings, vecAddresses, nVotingPower, mapResults);

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
                            .arg(vote.GetVotingPower()/COIN)
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

        SmartProposalVote storeVote(vote);

        storeVote.ResetVotingPower();

        for( auto result : results ){
            QJsonObject obj = result.toObject();

            QString status = obj["status"].toString();
            QString address = obj["smartAddress"].toString();
            double amount = obj["amount"].toDouble();

            QString resultString;

            if( status == "OK" ){
                resultString = SuccessText(status);
                storeVote.IncreaseVotingPower(amount * COIN);
            }else{
                resultString = ErrorText(status);
            }

            ui->results->append(QString("  -> %1 | %2 SMART <b>%3<b>").arg(address).arg((int)amount).arg(resultString));
        }

        if( storeVote.GetVotingPower() ){
            votingManager->Cache().AddVote(storeVote);
            votingManager->SyncCache();
        }

    }

    voteOne();
}
