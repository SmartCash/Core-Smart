#include "smartnodelist.h"
#include "ui_smartnodelist.h"


#include "clientmodel.h"
#include "init.h"
#include "guiutil.h"
#include "../smartnode/activesmartnode.h"
#include "../smartnode/smartnodesync.h"
#include "../smartnode/smartnodeconfig.h"
#include "../smartnode/smartnodeman.h"
#include "sync.h"
#include "wallet/wallet.h"
#include "walletmodel.h"
#include "nodecontroldialog.h"

#include <QTimer>
#include <QMessageBox>


bool SmartnodeWidgetItem::operator<(const QTableWidgetItem &other) const {

    const SmartnodeWidgetItem *otherSN = static_cast<const SmartnodeWidgetItem*>(&other);

    if (intValue != -1 && otherSN->intValue != -1){
        return intValue < otherSN->intValue;
    }

    return QTableWidgetItem::operator<(other);
}

int GetOffsetFromUtc()
{
#if QT_VERSION < 0x050200
    const QDateTime dateTime1 = QDateTime::currentDateTime();
    const QDateTime dateTime2 = QDateTime(dateTime1.date(), dateTime1.time(), Qt::UTC);
    return dateTime1.secsTo(dateTime2);
#else
    return QDateTime::currentDateTime().offsetFromUtc();
#endif
}

SmartnodeList::SmartnodeList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SmartnodeList),
    clientModel(0),
    walletModel(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMySmartnodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMySmartnodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMySmartnodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMySmartnodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMySmartnodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMySmartnodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetSmartnodes->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetSmartnodes->setColumnWidth(1, columnProtocolWidth);
    ui->tableWidgetSmartnodes->setColumnWidth(2, columnStatusWidth);
    ui->tableWidgetSmartnodes->setColumnWidth(3, columnActiveWidth);
    ui->tableWidgetSmartnodes->setColumnWidth(4, columnLastSeenWidth);

    ui->tableWidgetMySmartnodes->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction *startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMySmartnodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    fFilterUpdated = false;
    nTimeFilterUpdated = GetTime();
    updateNodeList();
}

SmartnodeList::~SmartnodeList()
{
    delete ui;
}

void SmartnodeList::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model) {
        // try to update list when smartnode count changes
        connect(clientModel, SIGNAL(strSmartnodesChanged(QString)), this, SLOT(updateNodeList()));
    }
}

void SmartnodeList::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
}

void SmartnodeList::showContextMenu(const QPoint &point)
{
    QTableWidgetItem *item = ui->tableWidgetMySmartnodes->itemAt(point);
    if(item) contextMenu->exec(QCursor::pos());
}

void SmartnodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH(CSmartnodeConfigEntry mne, smartnodeConfig.getEntries()) {
        if(mne.getAlias() == strAlias) {
            std::string strError;
            CSmartnodeBroadcast mnb;

            int nDos;
            bool fSuccess = CSmartnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if(fSuccess && !mnodeman.CheckMnbAndUpdateSmartnodeList(NULL, mnb, nDos, *g_connman)) {
                fSuccess = false;
                strError = "Please wait 15 confirmations or check your configuration";
            }

            if( fSuccess ){
                strStatusHtml += "<br>Successfully started smartnode.";
                mnb.Relay(*g_connman);
                mnodeman.NotifySmartnodeUpdates(*g_connman);
            } else {
                strStatusHtml += "<br>Failed to start smartnode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void SmartnodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH(CSmartnodeConfigEntry mne, smartnodeConfig.getEntries()) {
        std::string strError;
        CSmartnodeBroadcast mnb;

        int32_t nOutputIndex = 0;
        if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), nOutputIndex);

        if(strCommand == "start-missing" && mnodeman.Has(outpoint)) continue;

        int nDos;
        bool fSuccess = CSmartnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if(fSuccess && !mnodeman.CheckMnbAndUpdateSmartnodeList(NULL, mnb, nDos, *g_connman)) {
            fSuccess = false;
            strError = "Please wait 15 confirmations or check your configuration";
        }

        if( fSuccess ){
            nCountSuccessful++;
            mnb.Relay(*g_connman);
            mnodeman.NotifySmartnodeUpdates(*g_connman);
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }

    std::string returnObj;
    returnObj = strprintf("Successfully started %d smartnodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void SmartnodeList::updateMySmartnodeInfo(QString strAlias, QString strAddr, const COutPoint& outpoint)
{
    bool fOldRowFound = false;
    int nNewRow = 0;

    for(int i = 0; i < ui->tableWidgetMySmartnodes->rowCount(); i++) {
        if(ui->tableWidgetMySmartnodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if(nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMySmartnodes->rowCount();
        ui->tableWidgetMySmartnodes->insertRow(nNewRow);
    }

    smartnode_info_t infoMn;
    bool fFound = mnodeman.GetSmartnodeInfo(outpoint, infoMn);

    SmartnodeWidgetItem *aliasItem = new SmartnodeWidgetItem(strAlias);
    SmartnodeWidgetItem *addrItem = new SmartnodeWidgetItem(fFound ? QString::fromStdString(infoMn.addr.ToString()) : strAddr);
    SmartnodeWidgetItem *protocolItem = new SmartnodeWidgetItem(QString::number(fFound ? infoMn.nProtocolVersion : -1));
    SmartnodeWidgetItem *statusItem = new SmartnodeWidgetItem(QString::fromStdString(fFound ? CSmartnode::StateToString(infoMn.nActiveState) : "MISSING"));

    int activeSeconds = fFound ? (infoMn.nTimeLastPing - infoMn.sigTime) : 0;
    activeSeconds = activeSeconds < 0 ? 0: activeSeconds;

    QString activeSecondsTitle = QString::fromStdString(DurationToDHMS(activeSeconds));
    SmartnodeWidgetItem *activeSecondsItem = new SmartnodeWidgetItem(activeSecondsTitle, activeSeconds);

    int lastSeen = fFound ? infoMn.nTimeLastPing + GetOffsetFromUtc() : 0;
    QString lastSeenTitle = QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M",lastSeen));
    SmartnodeWidgetItem *lastSeenItem = new SmartnodeWidgetItem(lastSeenTitle, lastSeen);

    SmartnodeWidgetItem *pubkeyItem = new SmartnodeWidgetItem(QString::fromStdString(fFound ? CBitcoinAddress(infoMn.pubKeyCollateralAddress.GetID()).ToString() : ""));

    ui->tableWidgetMySmartnodes->setItem(nNewRow, COLUMN_ALIAS, aliasItem);
    ui->tableWidgetMySmartnodes->setItem(nNewRow, COLUMN_ADDRESS, addrItem);
    ui->tableWidgetMySmartnodes->setItem(nNewRow, COLUMN_PROTOCOL, protocolItem);
    ui->tableWidgetMySmartnodes->setItem(nNewRow, COLUMN_STATUS, statusItem);
    ui->tableWidgetMySmartnodes->setItem(nNewRow, COLUMN_ACTIVE, activeSecondsItem);
    ui->tableWidgetMySmartnodes->setItem(nNewRow, COLUMN_LASTSEEN, lastSeenItem);
    ui->tableWidgetMySmartnodes->setItem(nNewRow, COLUMN_PUBKEY, pubkeyItem);
}

void SmartnodeList::updateMyNodeList(bool fForce)
{
    TRY_LOCK(cs_mymnlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my smartnode list only once in MY_SMARTNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_SMARTNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if(nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    if( fForce ){
        ui->tableWidgetMySmartnodes->clearContents();
        ui->tableWidgetMySmartnodes->setRowCount(0);
    }

    ui->tableWidgetMySmartnodes->setSortingEnabled(false);

    BOOST_FOREACH(CSmartnodeConfigEntry mne, smartnodeConfig.getEntries()) {
        int32_t nOutputIndex = 0;
        if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        updateMySmartnodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), COutPoint(uint256S(mne.getTxHash()), nOutputIndex));
    }
    ui->tableWidgetMySmartnodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void SmartnodeList::updateNodeList()
{
    TRY_LOCK(cs_mnlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }

    if( ShutdownRequested() ) delete timer;

    static int64_t nTimeListUpdated = GetTime();

    // to prevent high cpu usage update only once in SMARTNODELIST_UPDATE_SECONDS seconds
    // or SMARTNODELIST_FILTER_COOLDOWN_SECONDS seconds after filter was last changed
    int64_t nSecondsToWait = fFilterUpdated
                            ? nTimeFilterUpdated - GetTime() + SMARTNODELIST_FILTER_COOLDOWN_SECONDS
                            : nTimeListUpdated - GetTime() + SMARTNODELIST_UPDATE_SECONDS;

    if(fFilterUpdated) ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));
    if(nSecondsToWait > 0) return;

    nTimeListUpdated = GetTime();
    fFilterUpdated = false;

    QString strToFilter;
    ui->countLabel->setText("Updating...");
    ui->tableWidgetSmartnodes->setSortingEnabled(false);
    ui->tableWidgetSmartnodes->clearContents();
    ui->tableWidgetSmartnodes->setRowCount(0);
    std::map<COutPoint, CSmartnode> mapSmartnodes = mnodeman.GetFullSmartnodeMap();

    int offsetFromUtc = GetOffsetFromUtc();

    for(auto& mnpair : mapSmartnodes)
    {
        CSmartnode mn = mnpair.second;
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        SmartnodeWidgetItem *addressItem = new SmartnodeWidgetItem(QString::fromStdString(mn.addr.ToString()));
        SmartnodeWidgetItem *protocolItem = new SmartnodeWidgetItem(QString::number(mn.nProtocolVersion));
        SmartnodeWidgetItem *statusItem = new SmartnodeWidgetItem(QString::fromStdString(mn.GetStatus()));

        int activeSeconds = mn.lastPing.sigTime - mn.sigTime;
        activeSeconds = activeSeconds < 0 ? 0: activeSeconds;

        QString activeSecondsTitle = QString::fromStdString(DurationToDHMS(activeSeconds));
        SmartnodeWidgetItem *activeSecondsItem = new SmartnodeWidgetItem(activeSecondsTitle, activeSeconds);

        int lastSeen = mn.lastPing.sigTime + offsetFromUtc;
        QString lastSeenTitle = QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M",lastSeen));
        SmartnodeWidgetItem *lastSeenItem = new SmartnodeWidgetItem(lastSeenTitle, lastSeen);

        SmartnodeWidgetItem *pubkeyItem = new SmartnodeWidgetItem(QString::fromStdString(CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString()));

        if (strCurrentFilter != "")
        {
            strToFilter =   addressItem->text() + " " +
                            protocolItem->text() + " " +
                            statusItem->text() + " " +
                            activeSecondsItem->text() + " " +
                            lastSeenItem->text() + " " +
                            pubkeyItem->text();
            if (!strToFilter.contains(strCurrentFilter)) continue;
        }

        ui->tableWidgetSmartnodes->insertRow(0);
        ui->tableWidgetSmartnodes->setItem(0, 0, addressItem);
        ui->tableWidgetSmartnodes->setItem(0, 1, protocolItem);
        ui->tableWidgetSmartnodes->setItem(0, 2, statusItem);
        ui->tableWidgetSmartnodes->setItem(0, 3, activeSecondsItem);
        ui->tableWidgetSmartnodes->setItem(0, 4, lastSeenItem);
        ui->tableWidgetSmartnodes->setItem(0, 5, pubkeyItem);
    }

    ui->countLabel->setText(QString::number(ui->tableWidgetSmartnodes->rowCount()));
    ui->tableWidgetSmartnodes->setSortingEnabled(true);
}

void SmartnodeList::on_filterLineEdit_textChanged(const QString &strFilterIn)
{
    strCurrentFilter = strFilterIn;
    nTimeFilterUpdated = GetTime();
    fFilterUpdated = true;
    ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", SMARTNODELIST_FILTER_COOLDOWN_SECONDS)));
}

void SmartnodeList::on_startButton_clicked()
{
    if(!smartnodeSync.IsSmartnodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until the SmartNode list is synced"));
        return;
    }

    std::string strAlias;
    {
        LOCK(cs_mymnlist);
        // Find selected node alias
        QItemSelectionModel* selectionModel = ui->tableWidgetMySmartnodes->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();

        if(selected.count() == 0) return;

        QModelIndex index = selected.at(0);
        int nSelectedRow = index.row();
        strAlias = ui->tableWidgetMySmartnodes->item(nSelectedRow, 0)->text().toStdString();
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm SmartNode start"),
        tr("Are you sure you want to start SmartNode %1? This will reset your node in the payment queue.").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

// void SmartnodeList::on_startAllButton_clicked()
// {
//     // Display message box
//     QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all SmartNodes start"),
//         tr("Are you sure you want to start ALL SmartNodes?"),
//         QMessageBox::Yes | QMessageBox::Cancel,
//         QMessageBox::Cancel);

//     if(retval != QMessageBox::Yes) return;

//     WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

//     if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
//         WalletModel::UnlockContext ctx(walletModel->requestUnlock());

//         if(!ctx.isValid()) return; // Unlock wallet was cancelled
//     }

//     StartAll(encStatus == walletModel->Locked);
// }

void SmartnodeList::on_startMissingButton_clicked()
{

    if(!smartnodeSync.IsSmartnodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until the SmartNode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing SmartNodes start"),
        tr("Are you sure you want to start MISSING SmartNodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void SmartnodeList::on_tableWidgetMySmartnodes_itemSelectionChanged()
{
    if(ui->tableWidgetMySmartnodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
        ui->EditButton->setEnabled(true);
        ui->RemoveButton->setEnabled(true);
        ui->ViewButton->setEnabled(true);
    }else{
        ui->startButton->setEnabled(false);
        ui->EditButton->setEnabled(false);
        ui->RemoveButton->setEnabled(false);
        ui->ViewButton->setEnabled(false);
    }
}

void SmartnodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}

void SmartnodeList::showControlDialog(SmartnodeControlMode mode)
{
    SmartnodeControlDialog dlg(platformStyle, mode);

    if( mode != SmartnodeControlMode::Create ){

        int entryIndex = 0;
        QString alias, ip, smartnodeKey, txHash, txIndex;

        QItemSelectionModel *select = ui->tableWidgetMySmartnodes->selectionModel();
        int row = select->selectedIndexes()[0].row();

        alias = ui->tableWidgetMySmartnodes->item(row, COLUMN_ALIAS)->text();

        auto aliasExists = std::find_if(smartnodeConfig.getEntries().begin(),
                                        smartnodeConfig.getEntries().end(),
                                        [alias](const CSmartnodeConfigEntry &entry) -> bool {
            return entry.getAlias() == alias.toStdString();
        });

        if( aliasExists == smartnodeConfig.getEntries().end() ){
            QMessageBox::critical(this, tr("Error"),
                tr("Could not find the selected alias. Restart your wallet and try it again."));
            return;
        }

        for( auto entry : smartnodeConfig.getEntries() ){
            if( entry.getPrivKey() == aliasExists->getPrivKey() ){
                break;
            }
            entryIndex++;
        }

        alias = QString::fromStdString(aliasExists->getAlias());
        ip = QString::fromStdString(aliasExists->getIp());
        smartnodeKey = QString::fromStdString(aliasExists->getPrivKey());
        txHash = QString::fromStdString(aliasExists->getTxHash());
        txIndex = QString::fromStdString(aliasExists->getOutputIndex());

        dlg.setSmartnodeData(entryIndex, alias, ip, smartnodeKey, txHash, txIndex);
    }

    dlg.setModel(walletModel);
    dlg.exec();
}

void SmartnodeList::on_CreateButton_clicked()
{
    showControlDialog(SmartnodeControlMode::Create);
    updateMyNodeList(true);
}

void SmartnodeList::on_EditButton_clicked()
{
    showControlDialog(SmartnodeControlMode::Edit);
    updateMyNodeList(true);
}

void SmartnodeList::on_RemoveButton_clicked()
{
    QString alias;
    std::string smartnodeKey, txHash, txIndex;

    QItemSelectionModel *select = ui->tableWidgetMySmartnodes->selectionModel();
    int row = select->selectedIndexes()[0].row();

    alias = ui->tableWidgetMySmartnodes->item(row, COLUMN_ALIAS)->text();

    auto aliasExists = std::find_if(smartnodeConfig.getEntries().begin(),
                                    smartnodeConfig.getEntries().end(),
                                    [alias](const CSmartnodeConfigEntry &entry) -> bool {
        return entry.getAlias() == alias.toStdString();
    });

    if( aliasExists == smartnodeConfig.getEntries().end() ){
        QMessageBox::critical(this, tr("Error"),
            tr("Could not find the selected alias. Restart your wallet and try it again."));
        return;
    }

    alias = QString::fromStdString(aliasExists->getAlias());
    smartnodeKey = aliasExists->getPrivKey();
    txHash = aliasExists->getTxHash();
    txIndex = aliasExists->getOutputIndex();

    if(QMessageBox::question(this, "Remove Smarnode entry", QString("Remove Smartnode %1?").arg(alias),
                             QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes){

        std::string strErr;

        if(!smartnodeConfig.Remove(smartnodeKey, strErr)){
            QMessageBox::critical(this, tr("Error"),
                tr("Could not remove the selected alias. Restart your wallet and try it again.\n\n") +
                QString::fromStdString(strErr));
            return;
        }

    }else{
        return;
    }

    QMessageBox::information(this, tr("Success"),
                                   QString("Smartnode %1 removed!").arg(alias),
                                   QMessageBox::Ok);

    COutPoint collateral(uint256S(txHash), std::atoi(txIndex.c_str()));
    walletModel->unlockCoin(collateral);

    updateMyNodeList(true);
}

void SmartnodeList::on_ViewButton_clicked()
{
    showControlDialog(SmartnodeControlMode::View);
}
