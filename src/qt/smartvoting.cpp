#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "smartvoting.h"
#include "ui_smartvoting.h"
#include "smartproposal.h"

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
    this->walletModel = model;

    connect(this->walletModel, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)),
            this,SLOT(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

    updateUI();
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

    for( SmartProposal *proposal : votingManager->GetProposals() ){

        SmartProposalWidget * proposalWidget = new SmartProposalWidget(proposal);

        if( votingManager->Cache().HasVote(proposal->getProposalId())){
            proposalWidget->setVoted(votingManager->Cache().GetVote(proposal->getProposalId()));
        }

        ui->proposalList->layout()->addWidget(proposalWidget);
        vecProposalWidgets.push_back(proposalWidget);
        connect(proposalWidget,SIGNAL(voteChanged()), this, SLOT(voteChanged()));

        LogPrint("smartvoting", "SmartVotingPage::updateUI -- added proposal %s", proposal->getTitle().toStdString());
    }

    voteChanged();

    ui->openProposalsLabel->setText(QString("%1").arg(votingManager->GetProposals().size()));

}

void SmartVotingPage::updateUI()
{

    // If the wallet model hasn't been set yet we cant update the UI.
    if(!walletModel) {
        return;
    }

    int nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();

    vecAddresses.clear();
    nVotingPower = 0;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    map<CTxDestination, CAmount> balances = pwalletMain->GetAddressBalances();
    BOOST_FOREACH(set<CTxDestination> grouping, pwalletMain->GetAddressGroupings())
    {
        BOOST_FOREACH(CTxDestination address, grouping)
        {
            if( balances[address] >= COIN ){
                vecAddresses.push_back(CBitcoinAddress(address).ToString());
                nVotingPower += balances[address];
            }
        }
    }

    QString votingPowerString = QString::number(nVotingPower / COIN);

    QChar thin_sp(THIN_SP_CP);
    int q_size = votingPowerString.size();

    for (int i = 3; i < q_size; i += 3)
        votingPowerString.insert(q_size - i, thin_sp);

    ui->votingPowerLabel->setText(votingPowerString  + " " +  BitcoinUnits::name(nDisplayUnit) );
    ui->addressesLabel->setText( QString("%1 addresses").arg(vecAddresses.size()));
}

void SmartVotingPage::proposalsUpdated(const string &strErr)
{
    updateProposalUI();
}

void SmartVotingPage::voteChanged(){

    mapVoteProposals.clear();

    for( SmartProposalWidget * proposalWidget : vecProposalWidgets)
        if( proposalWidget->getVoteType() != SmartHiveVoting::Disabled ){
            mapVoteProposals.insert(make_pair(proposalWidget->proposal, proposalWidget->getVoteType()));
        };

    ui->castVotesButton->setEnabled(mapVoteProposals.size() > 0 && nVotingPower);
    ui->castVotesButton->setText(QString("Vote for %1 proposals").arg(mapVoteProposals.size()));
    this->repaint();
}

void SmartVotingPage::castVotes(){

    CastVotesDialog dialog(platformStyle, votingManager);
    dialog.setVoting(mapVoteProposals, nVotingPower);
    dialog.setAddresses(vecAddresses);

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        dialog.exec();
    }else{
        dialog.exec();
    }

    refreshProposals(true);
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
