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
#include "smartvoting/votevalidation.h"
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


CastVotesDialog::CastVotesDialog(const PlatformStyle *platformStyle,
                                 WalletModel *model,
                                 const std::map<uint256, std::pair<vote_signal_enum_t, vote_outcome_enum_t>> &mapVotings,
                                 QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint),
    ui(new Ui::CastVotesDialog),
    platformStyle(platformStyle),
    walletModel(model),
    mapVotings(mapVotings)
{
    ui->setupUi(this);

    connect(ui->button, SIGNAL(clicked()), this, SLOT(close()));

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

bool CastVotesDialog::castVote( const CVoteKeySecret &voteKeySecret, const uint256 &hash, const vote_signal_enum_t eVoteSignal, const vote_outcome_enum_t eVoteOutcome, QString &strError )
{
    CVoteKey voteKey(voteKeySecret.GetKey().GetPubKey().GetID());

    strError = "";

    CProposalVote vote(voteKey, hash, eVoteSignal, eVoteOutcome);
    if(vote.Sign(voteKeySecret)) {

        CSmartVotingException exception;
        if(!smartVoting.ProcessVoteAndRelay(vote, exception, *g_connman)) {
            strError = QString::fromStdString(exception.GetMessage());
        }

    }else{
        strError = "Failed to sign the vote";
    }

    return strError == "";
}
void CastVotesDialog::start()
{

    if( !walletModel){
        ui->results->append(ErrorText("<br><b>Wallet not available!</b>"));
        ui->button->setText("Close");
        return;
    }

    std::map<SmartProposalVote, std::string> mapResults;
    std::set<CKeyID> setKeyIds;
    int activeCount = walletModel->voteKeyCount(true);

    walletModel->VoteKeyIDs(setKeyIds);

    ui->results->append(QString("<br>Signing overall <b>%1</b> message%2 for <b>%3</b> proposal%4.<br>")
                        .arg(activeCount * mapVotings.size())
                        .arg(activeCount * mapVotings.size() > 1 ? "s" : "")
                        .arg(mapVotings.size())
                        .arg(mapVotings.size() > 1 ? "s" : ""));

    WalletModel::EncryptionStatus encVotingStatus = walletModel->getVotingEncryptionStatus();
    bool fVotingLocked = encVotingStatus == WalletModel::Locked;

    std::unique_ptr<WalletModel::VotingUnlockContext> unlockVoting = fVotingLocked ?
                std::unique_ptr<WalletModel::VotingUnlockContext>(new WalletModel::VotingUnlockContext(walletModel->requestVotingUnlock())) :
                std::unique_ptr<WalletModel::VotingUnlockContext>(nullptr);

    if( unlockVoting.get() && !unlockVoting->isValid() ){
        ui->results->append(ErrorText("<br><b>Vote storage unlock failed!</b>"));
        ui->button->setText("Close");
        return;
    }


    for( auto vote : mapVotings ){

        const uint256 &hash = vote.first;
        const std::pair<vote_signal_enum_t, vote_outcome_enum_t> &voteType = vote.second;

        ui->results->append(QString("<br>Vote <b>%1</b> for <b>%2</b> with <b>%3 SMART</b> for proposal <b>%4</b><br><br>")
                            .arg(QString::fromStdString(CProposalVoting::ConvertOutcomeToString(voteType.second)))
                            .arg(QString::fromStdString(CProposalVoting::ConvertSignalToString(voteType.first)))
                            .arg(walletModel->votingPowerString(true))
                            .arg(QString::fromStdString(hash.ToString())));

        for( auto keyId : setKeyIds ){
            if( !pwalletMain->mapVotingKeyMetadata[keyId].fEnabled ) continue;
            CVoteKey vk(keyId);
            CKey secret;
            if( !pwalletMain->GetVotingKey(keyId, secret) ){
                ui->results->append(ErrorText("ERROR ") + QString("Failed to load the secret of %1").arg(QString::fromStdString(vk.ToString())));
                continue;
            }

            QString strError;

            if( !castVote(secret, hash, voteType.first, voteType.second, strError ) ){
                ui->results->append(ErrorText("ERROR ") + QString("Failed to vote with %1 - %2")
                                    .arg(QString::fromStdString(vk.ToString()))
                                    .arg(strError));
            }else{
                int nPower = GetVotingPower(vk);
                nPower = std::max<int>(0,nPower);

                ui->results->append(QString("%1 | %2 SMART <b>%3<b>")
                                    .arg(QString::fromStdString(vk.ToString()))
                                    .arg(nPower)
                                    .arg(SuccessText("OK")));
            }
        }
    }


}
