#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "smartvoting.h"
#include "ui_smartvoting.h"
#include "smartproposal.h"
#include "smartproposaltab.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "bitcoingui.h"
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
#include "smartvoting/votevalidation.h"

#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QScrollBar>
#include <QDateTime>
#include <QInputDialog>

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

bool VoteKeyWidgetItem::operator<(const QTableWidgetItem &other) const {
    int column = other.column();
    if (column == VoteKeyWidgetItem::COLUMN_POWER){
        QString t1 = text();
        QString t2 = other.text();

        t1 = t1.simplified();
        t1.replace( " ", "" );
        t1.replace("SMART", "");

        t2 = t2.simplified();
        t2.replace( " ", "" );
        t2.replace("SMART", "");

        return t1.toInt() < t2.toInt();

    }
    return QTableWidgetItem::operator<(other);
}

SmartVotingPage::SmartVotingPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SmartVotingPage),
    platformStyle(platformStyle),
    walletModel(nullptr),
    votingManager(new SmartVotingManager())
{
    ui->setupUi(this);

    QTableWidget *table = ui->voteKeysTable;

    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSortingEnabled(true);
    table->setShowGrid(false);
    table->verticalHeader()->hide();

    table->horizontalHeader()->setSectionResizeMode(VoteKeyWidgetItem::COLUMN_KEY, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(VoteKeyWidgetItem::COLUMN_ADDRESS, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(VoteKeyWidgetItem::COLUMN_POWER, QHeaderView::Stretch);

    connect(votingManager, SIGNAL(proposalsUpdated(const std::string&)),this,SLOT(proposalsUpdated(const std::string&)));
    connect(votingManager, SIGNAL(addressesUpdated()), this, SLOT(updateUI()));
    connect(ui->manageProposalsButton, SIGNAL(clicked()),this,SLOT(showManagementUI()));
    connect(ui->manageVotingKeysButton, SIGNAL(clicked()),this,SLOT(showVoteKeysUI()));
    connect(ui->createProposalButton, SIGNAL(clicked()),this,SLOT(createProposal()));

    connect(ui->backButton, SIGNAL(clicked()),this,SLOT(showVotingUI()));
    connect(ui->vkBackButton, SIGNAL(clicked()),this,SLOT(showVotingUI()));

    connect(ui->importVoteKeyButton, SIGNAL(clicked()), this, SLOT(importVoteKey()));

    connect(ui->selectAddressesButton, SIGNAL(clicked()),this,SLOT(selectAddresses()));
    connect(ui->castVotesButton, SIGNAL(clicked()),this,SLOT(castVotes()));
    connect(ui->refreshButton, SIGNAL(clicked()),this,SLOT(refreshProposals()));
    connect(ui->scrollArea->verticalScrollBar(), SIGNAL(valueChanged(int)),this,SLOT(scrollChanged(int)));
    connect(&lockTimer, SIGNAL(timeout()), this, SLOT(updateRefreshLock()));
    connect(&voteKeyUpdateTimer, SIGNAL(timeout()), this, SLOT(updateVoteKeyUI()));
    voteChanged();

    lockTimer.start(1000);

    showVotingUI();

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

void SmartVotingPage::connectProposalTab(SmartProposalTabWidget *tabWidget)
{
    connect(tabWidget, SIGNAL(titleChanged(SmartProposalTabWidget*, std::string&)), this, SLOT(tabTitleChanged(SmartProposalTabWidget*, std::string&)));
    connect(tabWidget, SIGNAL(removeButtonClicked(SmartProposalTabWidget*)), this, SLOT(removalRequested(SmartProposalTabWidget*)));
}

void SmartVotingPage::disconnectProposalTab(SmartProposalTabWidget *tabWidget)
{
    disconnect(tabWidget, SIGNAL(titleChanged(SmartProposalTabWidget*, std::string)), this, SLOT(tabTitleChanged(SmartProposalTabWidget*, std::string)));
    disconnect(tabWidget, SIGNAL(removeButtonClicked(SmartProposalTabWidget*)), this, SLOT(removalRequested(SmartProposalTabWidget*)));
}

bool SmartVotingPage::LoadProposalTabs()
{
    static bool fLoaded = false;

    if( fLoaded )
        return true;

    if( !pwalletMain )
        return false;

    CWalletDB walletdb(pwalletMain->strWalletFile);

    std::map<uint256, CInternalProposal> mapProposals;

    mapProposals.clear();

    walletdb.ReadProposals(mapProposals);

    for( auto it : mapProposals ){

        SmartProposalTabWidget * newProposalTab = new SmartProposalTabWidget(it.second, walletModel);

        connectProposalTab(newProposalTab);

        ui->proposalTabs->addTab(newProposalTab, QString::fromStdString(it.second.GetTitle()));
    }

    fLoaded = true;

    return true;
}

bool SmartVotingPage::RemoveProposal(const CInternalProposal& proposal)
{
    if( !pwalletMain )
        return false;

    CWalletDB walletdb(pwalletMain->strWalletFile);

    std::map<uint256, CInternalProposal> mapProposals;

    mapProposals.clear();

    walletdb.ReadProposals(mapProposals);

    mapProposals.erase(proposal.GetInternalHash());

    walletdb.WriteProposals(mapProposals);

    return true;
}

void SmartVotingPage::showManagementUI()
{

    if( !LoadProposalTabs() ){
        showErrorDialog(this, "Failed to load proposals.");
        return;
    }

    ui->stackedWidget->setCurrentIndex(0);
}

void SmartVotingPage::showVoteKeysUI()
{
    voteKeyUpdateTimer.start(60000);
    updateVoteKeyUI();
    ui->stackedWidget->setCurrentIndex(3);
}

void SmartVotingPage::showVotingUI()
{
    voteKeyUpdateTimer.stop();
    ui->stackedWidget->setCurrentIndex(2);
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

    ui->votingPowerLabel->setText( walletModel->enabledVotingPowerString() );
    ui->addressesLabel->setText( QString("%1 VoteKeys").arg(walletModel->enabledVoteKeys()));

    voteChanged();
}

void SmartVotingPage::proposalsUpdated(const string &strErr)
{
    if( strErr != ""){
        QMessageBox::warning(this, "Error", QString("Could not update proposal list\n\n%1").arg(QString::fromStdString(strErr)));
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

//    ui->castVotesButton->setEnabled(mapVoteProposals.size() && votingManager->GetVotingPower());
    ui->castVotesButton->setText(QString("Vote for %1 proposals").arg(mapVoteProposals.size()));

    this->repaint();
}

void SmartVotingPage::selectAddresses(){

    VoteAddressesDialog dialog(platformStyle, walletModel);
    dialog.exec();

    updateUI();
}

void SmartVotingPage::castVotes(){

    CastVotesDialog dialog(platformStyle, votingManager, walletModel);
    dialog.setVoting(mapVoteProposals);

    dialog.exec();

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
//    votingManager->UpdateProposals();
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

void SmartVotingPage::createProposal()
{

    CInternalProposal newProposal(GetRandHash());
    SmartProposalTabWidget * newProposalTab = new SmartProposalTabWidget(newProposal, walletModel);

    connectProposalTab(newProposalTab);

    ui->proposalTabs->addTab(newProposalTab, "New proposal");
    ui->proposalTabs->setCurrentIndex(ui->proposalTabs->count()-1);
}

void SmartVotingPage::tabTitleChanged(SmartProposalTabWidget* tab, string &newTitle)
{
    int idx = ui->proposalTabs->indexOf(tab);
    ui->proposalTabs->setTabText(idx, QString::fromStdString(newTitle));
}

void SmartVotingPage::removalRequested(SmartProposalTabWidget *tab)
{
    const CInternalProposal proposal = tab->GetProposal();

    if( !RemoveProposal(proposal) ){
        showErrorDialog(this, "Failed to remove proposal.");
        return;
    }

    int idx = ui->proposalTabs->indexOf(tab);
    ui->proposalTabs->removeTab(idx);
    delete tab;
}

void SmartVotingPage::updateVoteKeyUI()
{
    std::function<VoteKeyWidgetItem * (QString)> createItem = [](QString title) {
        VoteKeyWidgetItem * item = new VoteKeyWidgetItem(title);
        return item;
    };

    if( !pwalletMain || !walletModel ) return;

    std::set<CKeyID> setVotingKeyIds;
    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->GetVotingKeys(setVotingKeyIds);
    }

    int nRow = 0;

    QTableWidget *table = ui->voteKeysTable;

    table->clearContents();
    table->setRowCount(0);

    table->setSortingEnabled(false);
    for( auto keyId : setVotingKeyIds){

        table->insertRow(nRow);

        CVoteKey voteKey(keyId);

        std::string voteAddressString = "Not registered";

        CVoteKeyValue voteKeyValue;
        if( GetVoteKeyValue(voteKey, voteKeyValue) ){
            voteAddressString = voteKeyValue.voteAddress.ToString();
        }

        table->setItem(nRow, VoteKeyWidgetItem::COLUMN_KEY, createItem(QString::fromStdString(voteKey.ToString())));
        table->setItem(nRow, VoteKeyWidgetItem::COLUMN_ADDRESS, createItem(QString::fromStdString(voteAddressString)));
        table->setItem(nRow, VoteKeyWidgetItem::COLUMN_POWER, createItem(walletModel->votingPowerString(voteKey)));

        nRow++;
    }

    table->setSortingEnabled(true);
}

void SmartVotingPage::importVoteKey()
{

    if( !pwalletMain ) return;

    QString keyStr = QInputDialog::getText(this,"Import VoteKey secret","Insert your VoteKey secret here...");

    if( keyStr == QString() ) return;

    CVoteKeySecret voteKeySecret;

    if( !voteKeySecret.SetString( keyStr.toStdString() ) ){

        QMessageBox::critical(this, tr("Error"),
                                    tr("Invalid VoteKey secret provided\n\n") +
                                    keyStr,
                                    QMessageBox::Ok);
        return;
    }

    {
        LOCK(pwalletMain->cs_wallet);

        CKeyID keyId = voteKeySecret.GetKey().GetPubKey().GetID();

        if( pwalletMain->HaveVotingKey(keyId) ){

            QMessageBox::critical(this, tr("Error"),
                                        tr("The provided VotingKey secret already exists in the voting storage.\n\n") +
                                        keyStr,
                                        QMessageBox::Ok);
            return;
        }

        WalletModel::EncryptionStatus encStatus = walletModel->getVotingEncryptionStatus();
        bool fLocked = encStatus == WalletModel::Locked;

        std::unique_ptr<WalletModel::VotingUnlockContext> ctx = fLocked ?
                    std::unique_ptr<WalletModel::VotingUnlockContext>(new WalletModel::VotingUnlockContext(walletModel->requestVotingUnlock())) :
                    std::unique_ptr<WalletModel::VotingUnlockContext>(nullptr);

        if( ctx.get() && !ctx->isValid() )
            return;

        if( !pwalletMain->AddVotingKey(voteKeySecret.GetKey()) ){

            QMessageBox::critical(this, tr("Error"),
                                        tr("Failed to import VoteKey secret\n\n") +
                                        keyStr,
                                        QMessageBox::Ok);
            return;
        }

        if( !pwalletMain->HaveVotingKey(keyId) ){
            QMessageBox::critical(this, tr("Error"),
                                        tr("VoteKey is not available in the voting storage\n\n") +
                                        keyStr,
                                        QMessageBox::Ok);
            return;
        }

        CVotingKeyMetadata meta = pwalletMain->mapVotingKeyMetadata[keyId];
        meta.fImported = true;

        if( !pwalletMain->UpdateVotingKeyMetadata(keyId, meta) ){
            QMessageBox::critical(this, tr("Error"),
                                        tr("Failed to update the VoteKey's metadata\n\n") +
                                        keyStr,
                                        QMessageBox::Ok);
            return;
        }
    }

    CVoteKey voteKey(voteKeySecret.GetKey().GetPubKey().GetID());

    updateVoteKeyUI();

    QMessageBox::information(this, tr("Success"),
                                   QString("VoteKey %1 imported!").arg(QString::fromStdString(voteKey.ToString())),
                                   QMessageBox::Ok);
}

