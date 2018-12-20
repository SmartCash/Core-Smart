#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "smartvoting.h"
#include "ui_smartvoting.h"
#include "smartproposal.h"
#include "smartproposaltab.h"

#include "askpassphrasedialog.h"
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
#include "smartvoting/votevalidation.h"
#include "specialtransactiondialog.h"

#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QScrollBar>
#include <QDateTime>
#include <QInputDialog>

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
    walletModel(nullptr)
{
    ui->setupUi(this);

    mapVisibleKeys.clear();

    QTableWidget *table = ui->voteKeysTable;

    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSortingEnabled(true);
    table->setShowGrid(false);
    table->verticalHeader()->hide();

    table->horizontalHeader()->setSectionResizeMode(VoteKeyWidgetItem::COLUMN_CHECKBOX, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(VoteKeyWidgetItem::COLUMN_KEY, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(VoteKeyWidgetItem::COLUMN_ADDRESS, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(VoteKeyWidgetItem::COLUMN_POWER, QHeaderView::Stretch);

    table->clearContents();
    table->setRowCount(0);

    connect(ui->manageProposalsButton, SIGNAL(clicked()),this,SLOT(showManagementUI()));
    connect(ui->manageVotingKeysButton, SIGNAL(clicked()),this,SLOT(showVoteKeysUI()));
    connect(ui->createProposalButton, SIGNAL(clicked()),this,SLOT(createProposal()));

    connect(ui->backButton, SIGNAL(clicked()),this,SLOT(showVotingUI()));
    connect(ui->vkBackButton, SIGNAL(clicked()),this,SLOT(showVotingUI()));

    connect(ui->voteKeysTable, SIGNAL(cellChanged(int,int)), this, SLOT(voteKeyCellChanged(int,int)));
    connect(ui->changeAllButton, SIGNAL(clicked()), this, SLOT(selectAllVoteKeys()));
    connect(ui->importVoteKeyButton, SIGNAL(clicked()), this, SLOT(importVoteKey()));
    connect(ui->registerVoteKeyButton, SIGNAL(clicked()), this, SLOT(registerVoteKey()));

    connect(ui->castVotesButton, SIGNAL(clicked()),this,SLOT(castVotes()));
    connect(ui->scrollArea->verticalScrollBar(), SIGNAL(valueChanged(int)),this,SLOT(scrollChanged(int)));
    connect(&lockTimer, SIGNAL(timeout()), this, SLOT(updateProposalUI()));
    connect(&voteKeyUpdateTimer, SIGNAL(timeout()), this, SLOT(updateVoteKeyUI()));
    voteChanged();

    lockTimer.start(5000);

    showVotingUI();

}

bool SmartVotingPage::IsVotingEnabled()
{
    int nHeight = chainActive.Height();

    if( !fDebug && MainNet() && nHeight <= SMARTVOTING_START_HEIGHT ){
        QMessageBox::information(this, tr("Not yet!"),
                                       QString(("SmartVoting features will be available at block %1\n\n"
                                                "%2 blocks left.."))
                                            .arg(SMARTVOTING_START_HEIGHT)
                                            .arg(SMARTVOTING_START_HEIGHT - nHeight),
                                       QMessageBox::Ok);
        return false;
    }

    return true;
}

SmartVotingPage::~SmartVotingPage()
{
    mapVisibleKeys.clear();
    delete ui;
}

void SmartVotingPage::setWalletModel(WalletModel *model)
{
    if( walletModel ) return;

    walletModel = model;
    WalletModel::EncryptionStatus status = walletModel->getVotingEncryptionStatus();

    if( status != WalletModel::Unencrypted )
        ui->unencryptedWidget->hide();
    else
        connect(ui->encryptButton, SIGNAL(clicked()), this, SLOT(encryptVoting()));
}

void SmartVotingPage::showEvent(QShowEvent *event)
{
    updateVoteKeyUI();
    updateVotingElements();
    updateProposalUI();
    updateUI();
}

void SmartVotingPage::hideEvent(QHideEvent *event)
{

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

    if( !IsVotingEnabled() ) return;

    if( !LoadProposalTabs() ){
        showErrorDialog(this, "Failed to load proposals.");
        return;
    }

    ui->stackedWidget->setCurrentIndex(0);
}

void SmartVotingPage::encryptVoting()
{
    if( !walletModel ) return;

    AskPassphraseDialog dlg(AskPassphraseDialog::EncryptVoting, this);
    dlg.setModel(walletModel);
    dlg.exec();

    WalletModel::EncryptionStatus status = walletModel->getVotingEncryptionStatus();

    if( status != WalletModel::Unencrypted )
        ui->unencryptedWidget->hide();

}

void SmartVotingPage::showVoteKeysUI()
{
    if( !IsVotingEnabled() ) return;

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
    TRY_LOCK(smartVoting.cs, locked);
    if( !locked ){
        LogPrintf("SmartVotingPage::updateProposalUI lock failed\n");
        return;
    }

    int votedValid = 0, votedFunding = 0;

    std::vector<const CProposal*> vecProposals = smartVoting.GetAllNewerThan(0);

    // Search for proposals which are not in the view yet
    for( auto entry : vecProposals ){
        if( !mapProposalWidgets.count(entry->GetHash()) ){
            SmartProposalWidget * proposalWidget = new SmartProposalWidget(entry, walletModel);
            ui->proposalList->layout()->addWidget(proposalWidget);
            mapProposalWidgets.insert(std::make_pair(entry->GetHash(), proposalWidget));
            connect(proposalWidget,SIGNAL(voteChanged()), this, SLOT(voteChanged()));
        }else{
            mapProposalWidgets[entry->GetHash()]->UpdateFromProposal(entry);
        }
    }

    // Search for proposals that are currently in the view
    // but not longer in the proposal list
    auto widget = mapProposalWidgets.begin();
    while( widget != mapProposalWidgets.end() ){

        auto find = std::find_if(vecProposals.begin(), vecProposals.end(), [widget](const CProposal* p) -> bool {
            return widget->first == p->GetHash();
        });

        if( find == vecProposals.end()  ){
            int idx = ui->proposalList->layout()->indexOf(widget->second);
            QLayoutItem *child = ui->proposalList->layout()->takeAt(idx);
            delete child->widget();
            delete child;
            widget = mapProposalWidgets.erase(widget);
        }else{
            if( widget->second->votedValid() ) votedValid++;
            if( widget->second->votedFunding() ) votedFunding++;
            ++widget;
        }

    }

    voteChanged();

    ui->openProposalsLabel->setText(QString("%1").arg(vecProposals.size()));
    ui->votedForValidityLabel->setText(QString("%1").arg(votedValid));
    ui->votedForFundingLabel->setText(QString("%1").arg(votedFunding));
}

void SmartVotingPage::updateUI()
{

    // If the wallet model hasn't been set yet we cant update the UI.
    if(!walletModel) {
        return;
    }

    voteChanged();
}

void SmartVotingPage::voteChanged(){

    mapVoteProposals.clear();

    for( auto entry : mapProposalWidgets){
        if( entry.second->GetVoteSignal() != VOTE_SIGNAL_NONE ){
            mapVoteProposals.insert(make_pair(entry.first,
                                              make_pair(entry.second->GetVoteSignal(), entry.second->GetVoteOutcome())));
        };
    }

    ui->castVotesButton->setEnabled(mapVoteProposals.size() && walletModel->voteKeyCount(true));
    ui->castVotesButton->setText(QString("Vote for %1 proposals").arg(mapVoteProposals.size()));

    this->repaint();
}

void SmartVotingPage::castVotes(){

    CastVotesDialog dialog(platformStyle, walletModel, mapVoteProposals);
    dialog.exec();

    for( auto entry : mapProposalWidgets ){
        entry.second->ResetVoteSelection();
    }

    updateProposalUI();
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

void SmartVotingPage::updateVotingElements()
{
    if( !walletModel ) return;

    ui->labelVoteKeyCount->setText(QString::number(walletModel->voteKeyCount(false)));
    ui->labelActiveVoteKeyCount->setText(QString::number(walletModel->voteKeyCount(true)));

    ui->labelTotalPower->setText(walletModel->votingPowerString(false));
    ui->labelActivePower->setText(walletModel->votingPowerString(true));

    ui->votingPowerLabel->setText( walletModel->votingPowerString(true) );
    ui->manageVotingKeysButton->setText( QString("Manage voting keys (%1 active)").arg(walletModel->voteKeyCount(true) ) );
}

void SmartVotingPage::updateVoteKeyUI()
{
    std::function<VoteKeyWidgetItem * (QString)> createItem = [](QString title) {
        VoteKeyWidgetItem * item = new VoteKeyWidgetItem(title);
        return item;
    };

    if( !pwalletMain || !walletModel ) return;

    updateVotingElements();

    std::set<CKeyID> setVotingKeyIds;
    std::map<CKeyID, CVotingKeyMetadata> mapMeta;
    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->GetVotingKeys(setVotingKeyIds);
        mapMeta = pwalletMain->mapVotingKeyMetadata;
    }

    Qt::ItemFlags checkBoxEnabledFlags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    Qt::ItemFlags checkBoxDisabledFlags = Qt::ItemIsSelectable;

    QTableWidget *table = ui->voteKeysTable;
    int nRow = table->rowCount();

    table->setSortingEnabled(false);

    for( auto keyId : setVotingKeyIds ){

        CVoteKey voteKey(keyId);
        VoteKeyWidgetItem* checkBoxItem, *votingPowerItem, *voteAddressItem;

        auto it = mapVisibleKeys.find(keyId);

        if( it == mapVisibleKeys.end() ){

            table->insertRow(nRow);

            checkBoxItem = new VoteKeyWidgetItem();

            votingPowerItem = createItem(walletModel->votingPowerString(voteKey));
            voteAddressItem = createItem(walletModel->voteAddressString(voteKey));

            checkBoxItem->setFlags(checkBoxEnabledFlags);

            table->setItem(nRow, VoteKeyWidgetItem::COLUMN_CHECKBOX, checkBoxItem);
            table->setItem(nRow, VoteKeyWidgetItem::COLUMN_KEY, createItem(QString::fromStdString(voteKey.ToString())));
            table->setItem(nRow, VoteKeyWidgetItem::COLUMN_ADDRESS, voteAddressItem);
            table->setItem(nRow, VoteKeyWidgetItem::COLUMN_POWER, votingPowerItem);

            mapVisibleKeys.insert(std::make_pair(keyId,VoteKeyItems(checkBoxItem, voteAddressItem, votingPowerItem)));

            nRow++;
        }else{
            checkBoxItem = it->second.checkbox;
            votingPowerItem = it->second.power;
            voteAddressItem = it->second.address;
        }

        votingPowerItem->setText(walletModel->votingPowerString(voteKey));
        voteAddressItem->setText(walletModel->voteAddressString(voteKey));

        if( !IsRegisteredForVoting(voteKey) ){

            checkBoxItem->setFlags(checkBoxDisabledFlags);
            checkBoxItem->setCheckState(Qt::Unchecked);

        }else{

            checkBoxItem->setFlags(checkBoxEnabledFlags);

            if( mapMeta[keyId].fEnabled )
                checkBoxItem->setCheckState(Qt::Checked);
            else
                checkBoxItem->setCheckState(Qt::Unchecked);
        }

    }

    table->setSortingEnabled(true);
}

void SmartVotingPage::voteKeyCellChanged(int row, int column)
{
    if( !pwalletMain ) return;

    QTableWidgetItem *voteKeyItem = ui->voteKeysTable->item(row,VoteKeyWidgetItem::COLUMN_KEY);
    QTableWidgetItem *checkBoxItem = ui->voteKeysTable->item(row,VoteKeyWidgetItem::COLUMN_CHECKBOX);

    if( voteKeyItem && checkBoxItem ){

        QString voteKey = voteKeyItem->text();

        bool fChecked = checkBoxItem->checkState() == Qt::Checked;

        CVoteKey vk(voteKey.toStdString());
        CKeyID keyId;

        if( vk.GetKeyID(keyId) ){
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->mapVotingKeyMetadata[keyId].fEnabled = fChecked;
            pwalletMain->UpdateVotingKeyMetadata(keyId);
        }
    }

    updateVotingElements();
}


void SmartVotingPage::registerVoteKey()
{
    SpecialTransactionDialog dlg(REGISTRATION_TRANSACTIONS, platformStyle);
    dlg.setModel(walletModel);
    dlg.exec();
    updateVoteKeyUI();
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

        pwalletMain->mapVotingKeyMetadata[keyId].fImported = true;

        if( !pwalletMain->UpdateVotingKeyMetadata(keyId) ){
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

void SmartVotingPage::selectAllVoteKeys()
{
    if( !walletModel ) return;
    walletModel->updateVoteKeys(!walletModel->voteKeyCount(true));
    updateVoteKeyUI();
}
