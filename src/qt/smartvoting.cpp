#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "smartvoting.h"
#include "ui_smartvoting.h"
#include "smartproposal.h"
#include "smartrewards/rewards.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "txmempool.h"
#include "walletmodel.h"
#include "coincontrol.h"
#include "wallet/wallet.h"
#include "clientmodel.h"
#include "castvotesdialog.h"
#include "validation.h"
#include "voteaddressesdialog.h"
#include "specialtransactiondialog.h"

#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QScrollBar>
#include <QDateTime>

const int nRefreshLockSeconds = 120;
const int nForceRefreshSeconds = 300;
static int nLastRefreshTime = 0;

struct QSmartVortingField
{
    QString label;
    QString address;
    CAmount balance;

    QSmartVortingField() : label(QString()), address(QString()),
                          balance(0){}
};


SmartVotingPage::SmartVotingPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SmartVotingPage),
    platformStyle(platformStyle),
    walletModel(0)
{
    ui->setupUi(this);

    votingManager = new SmartVotingManager();

    connect(votingManager, SIGNAL(proposalsUpdated(const std::string&)),this,SLOT(proposalsUpdated(const std::string&)));
    connect(votingManager, SIGNAL(addressesUpdated()), this, SLOT(updateUI()));
    connect(ui->selectAddressesButton, SIGNAL(clicked()),this,SLOT(selectAddresses()));
    connect(ui->castVotesButton, SIGNAL(clicked()),this,SLOT(castVotes()));
    connect(ui->refreshButton, SIGNAL(clicked()),this,SLOT(refreshProposals()));
    connect(ui->scrollArea->verticalScrollBar(), SIGNAL(valueChanged(int)),this,SLOT(scrollChanged(int)));
    connect(&lockTimer, SIGNAL(timeout()), this, SLOT(updateRefreshLock()));
    voteChanged();

    lockTimer.start(1000);
}

SmartVotingPage::~SmartVotingPage()
{
    delete ui;
}

void SmartVotingPage::setWalletModel(WalletModel *model)
{
    if( walletModel ) return;

    walletModel = model;
    votingManager->setWalletModel(model);
}

void SmartVotingPage::showEvent(QShowEvent *event)
{
    int64_t nSecondsTillRefresh = nLastRefreshTime + nForceRefreshSeconds - GetTime();

    if( nSecondsTillRefresh <= 0 ){
        refreshProposals();
    }else{
        updateProposalUI();
    }

    updateUI();

}

void SmartVotingPage::hideEvent(QHideEvent *event)
{
    QLayoutItem * item;

    while( ( item = ui->proposalList->layout()->takeAt(0) ) != 0){
        delete item->widget();
        delete item;
    }

    vecProposalWidgets.clear();
}

void SmartVotingPage::updateProposalUI()
{
    QLayoutItem * item;

    while( ( item = ui->proposalList->layout()->takeAt(0) ) != 0){
        delete item->widget();
        delete item;
    }

    vecProposalWidgets.clear();

    int voted = 0;

    for( SmartProposal *proposal : votingManager->GetProposals() ){

        SmartProposalWidget * proposalWidget = new SmartProposalWidget(proposal);

        ui->proposalList->layout()->addWidget(proposalWidget);
        vecProposalWidgets.push_back(proposalWidget);
        connect(proposalWidget,SIGNAL(voteChanged()), this, SLOT(voteChanged()));

        if( proposalWidget->voted() ) voted++;

        LogPrint("smartvoting", "SmartVotingPage::updateUI -- added proposal %s", proposal->getTitle().toStdString());
    }

    voteChanged();

    ui->openProposalsLabel->setText(QString("%1").arg(votingManager->GetProposals().size()));
    ui->votedForLabel->setText(QString("%1").arg(voted));
}

void SmartVotingPage::updateUI()
{

    // If the wallet model hasn't been set yet we cant update the UI.
    if(!walletModel) {
        return;
    }

    QString votingPowerString = QString::number(std::round(votingManager->GetVotingPower()),'f',0);

    AddThousandsSpaces(votingPowerString);

    ui->votingPowerLabel->setText(votingPowerString  + " SMART" );
    ui->addressesLabel->setText( QString("%1 addresses").arg(votingManager->GetEnabledAddressCount()));

    voteChanged();
}

void SmartVotingPage::proposalsUpdated(const string &strErr)
{
    if( strErr != ""){
//        QMessageBox::warning(this, "Error", QString("Could not update proposal list\n\n%1").arg(QString::fromStdString(strErr)));
        return;
    }

    updateProposalUI();
}

void SmartVotingPage::voteChanged(){

    mapVoteProposals.clear();

    for( SmartProposalWidget * proposalWidget : vecProposalWidgets){
        if( proposalWidget->getVoteType() != SmartHiveVoting::Disabled ){
            mapVoteProposals.insert(make_pair(proposalWidget->proposal, proposalWidget->getVoteType()));
        };
    }

    ui->castVotesButton->setEnabled(mapVoteProposals.size() && votingManager->GetVotingPower());
    ui->castVotesButton->setText(QString("Vote for %1 proposals").arg(mapVoteProposals.size()));

    this->repaint();
}

void SmartVotingPage::selectAddresses(){

    VoteAddressesDialog dialog(platformStyle, votingManager);
    dialog.exec();

    updateUI();
}

void SmartVotingPage::castVotes(){

    CastVotesDialog dialog(platformStyle, votingManager, walletModel);

    dialog.setVoting(mapVoteProposals);

    dialog.exec();

    refreshProposals(true);
    uiInterface.NotifySmartRewardUpdate();
}

void SmartVotingPage::updateRefreshLock()
{
    int64_t nSecondsTillUnlock = nLastRefreshTime + nRefreshLockSeconds - GetTime();

    if( nSecondsTillUnlock <= 0 ){
        ui->refreshButton->setText("Refresh list");
        ui->refreshButton->setEnabled(true);
        lockTimer.stop();
        return;
    }

    ui->refreshButton->setText(QString("Locked (%1s)").arg(nSecondsTillUnlock));
}

void SmartVotingPage::refreshProposals(bool fForce)
{
    int64_t nSecondsTillUnlock = nLastRefreshTime + nRefreshLockSeconds - GetTime();

    if( nSecondsTillUnlock > 0 && !fForce ) return;

    nLastRefreshTime = GetTime();
    ui->refreshButton->setEnabled(false);
    lockTimer.start(1000);
    votingManager->UpdateProposals();
}

void SmartVotingPage::scrollChanged(int value)
{

    // Force redrawing every few scroll steps since the method used to
    // show multiple widgets in a scroll view is not ideal and causes
    // weird drawings from time to time...
    if (ui->scrollArea->verticalScrollBar()->maximum() == value ||
        ui->scrollArea->verticalScrollBar()->minimum() == value ||
        !( value % 30 )   )
        this->repaint();

}

void SmartVotingPage::balanceChanged(const CAmount &balance, const CAmount &unconfirmedBalance, const CAmount &immatureBalance, const CAmount &watchOnlyBalance, const CAmount &watchUnconfBalance, const CAmount &watchImmatureBalance)
{
    updateUI();
}

