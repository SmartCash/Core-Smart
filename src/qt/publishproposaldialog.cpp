// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "publishproposaldialog.h"
#include "ui_publishproposaldialog.h"

#include "bitcoingui.h"
#include "chainparams.h"
#include "guiutil.h"
#include "smartvoting/manager.h"
#include "util.h"
#include "validation.h"
#include "waitingspinnerwidget.h"

#include <regex>

#include <boost/foreach.hpp>

#include <QDesktopServices>
#include <QUrl>

PublishProposalDialog::PublishProposalDialog(const CInternalProposal& proposal, QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint),
    ui(new Ui::PublishProposalDialog),
    proposal(proposal)
{
    ui->setupUi(this);

    connect(ui->explorerButton, SIGNAL(clicked()), this, SLOT(openExplorer()));
    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(close()));

    WaitingSpinnerWidget * spinner = ui->spinnerWidget;

    spinner->setRoundness(70.0);
    spinner->setMinimumTrailOpacity(15.0);
    spinner->setTrailFadePercentage(70.0);
    spinner->setNumberOfLines(14);
    spinner->setLineLength(14);
    spinner->setLineWidth(6);
    spinner->setInnerRadius(20);
    spinner->setRevolutionsPerSecond(1);
    spinner->setColor(QColor(254, 198, 13));

    spinner->start();

    this->setWindowTitle("Publish proposal");

    connect(&timer, SIGNAL(timeout()), this, SLOT(update()));
    timer.start(5000);

    update();
}

PublishProposalDialog::~PublishProposalDialog()
{
    delete ui;
}

void PublishProposalDialog::openExplorer()
{
    QDesktopServices::openUrl(QUrl(QString::fromStdString("https://insight.smartcash.cc/tx/" + proposal.GetFeeHash().ToString())));
}

void PublishProposalDialog::close()
{
    done(QDialog::Accepted);
}

void PublishProposalDialog::update()
{
    CTransaction tx;
    uint256 blockHash;
    int nHeight = 0, nTxHeight = 0, nConfirmations = 0;
    const CChainParams& chainparams = Params();

    {
        LOCK(cs_main);
        nHeight = chainActive.Height();
    }

    if(GetTransaction(proposal.GetFeeHash(), tx, chainparams.GetConsensus(), blockHash)){

        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                nTxHeight = pindex->nHeight;
            }
        }

    }

    if( nTxHeight )
        nConfirmations = nHeight - nTxHeight + 1;

    if( nConfirmations >= SMARTVOTING_MIN_RELAY_FEE_CONFIRMATIONS){

        if(smartVoting.HaveProposalForHash(proposal.GetHash())) {
            LogPrint("proposal", "VOTINGPROPOSAL -- Received already seen object: %s\n", proposal.GetHash().ToString());
            return;
        }

        std::string strError = "";
        // CHECK PROPOSAL AGAINST LOCAL BLOCKCHAIN

        int fMissingConfirmations;
        bool fIsValid;
        {
            LOCK(cs_main);
            fIsValid = proposal.IsValidLocally(strError, fMissingConfirmations, true);
        }

        if(!fIsValid) {

            if( fMissingConfirmations ){
                smartVoting.AddPostponedProposal(proposal);
                LogPrintf("VOTINGPROPOSAL -- Not enough fee confirmations for: %s, strError = %s\n", proposal.GetHash().ToString(), strError);
            }else{

                ui->infoLabel->setText(("Failed to publish the proposal."));
                return;
            }

        }else{
            smartVoting.AddProposal(proposal, *g_connman);
        }

        ui->infoLabel->setText(("Your proposal has been published successfully!\n\n"
                                "To make the proposal more publicly available you should"
                                " consider adding it to the voting portal. Therefor you"
                                " can just close this dialog and then click the \"Detail\" button"
                                " to get your credentials."));


        Q_EMIT published();

        timer.stop();
        ui->loadingWidget->hide();

    }else{
        ui->confirmationsLabel->setText(QString("%1/%2").arg(nConfirmations).arg(SMARTVOTING_MIN_RELAY_FEE_CONFIRMATIONS));
    }

}
