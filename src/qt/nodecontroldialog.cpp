// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "nodecontroldialog.h"
#include "ui_nodecontroldialog.h"

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

QRegularExpression ipRegex("^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)(\\.|$)){3}((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)(\\:|$)){1}(\\d){0,5}$");

bool CSmartnodeControlWidgetItem::operator<(const QTableWidgetItem &other) const {
    int column = other.column();
    if (column == SmartnodeControlDialog::COLUMN_ADDRESS ||
        column == SmartnodeControlDialog::COLUMN_LABEL ||
        column == SmartnodeControlDialog::COLUMN_TXHASH){
        return data(Qt::UserRole).toString() < other.data(Qt::UserRole).toString();
    }else if (column == SmartnodeControlDialog::COLUMN_TXID)
        return data(Qt::UserRole).toLongLong() < other.data(Qt::UserRole).toLongLong();
    return QTableWidgetItem::operator<(other);
}

SmartnodeControlDialog::SmartnodeControlDialog(const PlatformStyle *platformStyle, SmartnodeControlMode mode, QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint),
    unlockedForEdit(COutPoint()),
    ui(new Ui::SmartnodeControlDialog),
    model(0),
    mode(mode),
    platformStyle(platformStyle)
{
    ui->setupUi(this);

    QTableWidget *collateralTable = ui->collateralTable;

    collateralTable->setAlternatingRowColors(true);
    collateralTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    collateralTable->setSelectionMode(QAbstractItemView::SingleSelection);
    collateralTable->setSortingEnabled(false);
    collateralTable->setShowGrid(false);
    collateralTable->verticalHeader()->hide();

    collateralTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    collateralTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    collateralTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    collateralTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    connect(ui->defaultButtonBox, SIGNAL(clicked( QAbstractButton*)), this, SLOT(buttonBoxClicked(QAbstractButton*)));
    connect(ui->viewButtonBox, SIGNAL(clicked( QAbstractButton*)), this, SLOT(buttonBoxClicked(QAbstractButton*)));
    connect(ui->copySmartnodeKeyButton, SIGNAL(clicked()), this, SLOT(copySmartnodeKey()));
    connect(ui->customSmartnodeKeyButton, SIGNAL(clicked()), this, SLOT(addCustomSmartnodeKey()));
    connect(collateralTable->horizontalHeader(), SIGNAL(sectionClicked(int)), this, SLOT(headerSectionClicked(int)));

    if( mode == SmartnodeControlMode::View ){
        ui->collateralView->setCurrentIndex(1);

        ui->aliasField->setEnabled(false);
        ui->ipField->setEnabled(false);
        ui->customSmartnodeKeyButton->hide();
        ui->defaultButtonBox->hide();
    }else{
        ui->collateralView->setCurrentIndex(0);

        ui->viewButtonBox->hide();
    }

    switch(mode){
    case SmartnodeControlMode::Create:
        this->setWindowTitle("Smartnode creation");
        break;
    case SmartnodeControlMode::Edit:
        this->setWindowTitle("Smartnode editing");
        break;
    case SmartnodeControlMode::View:
        this->setWindowTitle("Smartnode information");
        break;
    default:
        this->setWindowTitle("Smartnode?");
        break;
    }


}

SmartnodeControlDialog::~SmartnodeControlDialog()
{

    if( !unlockedForEdit.IsNull() ){
        model->lockCoin(unlockedForEdit);
    }

    delete ui;
}

void SmartnodeControlDialog::setSmartnodeData(int entryIndex, QString alias, QString ip, QString smartnodeKey, QString txHash, QString txIndex)
{
    this->entryIndex = entryIndex;
    ui->aliasField->setText(alias);
    ui->ipField->setText(ip);
    ui->smartnodeKeyLabel->setText(smartnodeKey);
    this->txHash = txHash.toStdString();
    this->txIndex = txIndex.toStdString();
}

void SmartnodeControlDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel() && model->getAddressTableModel())
    {
        updateView();
    }
}

void SmartnodeControlDialog::showError(QString message){
    QMessageBox::critical(this, tr("Error"),
                                tr(message.toStdString().c_str()),
                                QMessageBox::Ok);

    if( !unlockedForEdit.IsNull() ){
        model->lockCoin(unlockedForEdit);
    }

}

void SmartnodeControlDialog::buttonBoxClicked(QAbstractButton* button)
{
    if (ui->defaultButtonBox->buttonRole(button) == QDialogButtonBox::ApplyRole){

        std::string strErr;

        QString qalias = ui->aliasField->text();
        QString qip = ui->ipField->text();

        smartnodeKey = ui->smartnodeKeyLabel->text().toStdString();

        // Do basic tests for name and IP

        if( qalias == "" ){
            showError("Alias missing.");
            return;
        }else{
            LogPrintf("SmartnodeControlDialog -- valid alias: %s\n",qalias.toStdString());
        }

        QRegularExpressionMatch ipMatch = ipRegex.match(qip);

        if( qip == "" || !ipMatch.hasMatch() ){
            showError(tr("Invalid IP-Address") +
                      QString::fromStdString("\n\n") +
                      tr("Required format: xxx.xxx.xxx.xxx or xxx.xxx.xxx.xxx:port"));
            return;
        }else{
            if(!validateSmartnodeIPAddress(qip)){
              showError(tr("Invalid SmartNode IP-Address (Unreachable)"));
              return;
            }else{
              LogPrintf("SmartnodeControlDialog -- valid ip: %s\n",qip.toStdString());
            }
        }

        LogPrintf("SmartnodeControlDialog -- remove whitespaces\n");

        // Remove whitespaces to avoid parsing errors on start.
        qalias.replace(QRegularExpression("\\s+"), QString());
        qip.replace(QRegularExpression("\\s+"), QString());

        alias = qalias.toStdString();
        ip = qip.toStdString();

        LogPrintf("SmartnodeControlDialog -- search port\n");
        auto portStart = ip.find(":");

        if( portStart == std::string::npos ){

            LogPrintf("SmartnodeControlDialog -- use default port\n");
            int port = Params().GetDefaultPort();
            ip += ":" + std::to_string(port);

        }else{

            LogPrintf("SmartnodeControlDialog -- parse custom port\n");

            int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
            int port = atoi(ip.substr(portStart + 1).c_str());

            LogPrintf("SmartnodeControlDialog -- validate custom port\n");

            if(MainNet()) {
                if(port != mainnetDefaultPort) {
                    strErr = "Invalid port\n" +
                            strprintf("Port: %d", port) + "\n" +
                            strprintf("(must be %d for mainnet)", mainnetDefaultPort);
                    showError(QString::fromStdString(strErr));
                    return;
                }
            } else if(port == mainnetDefaultPort) {
                strErr = "Invalid port\n" +
                        strprintf("(%d could be used only on mainnet)", mainnetDefaultPort);
                showError(QString::fromStdString(strErr));
                return;
            }
        }

        LogPrintf("SmartnodeControlDialog -- check for collateral\n");

        QItemSelectionModel *select = ui->collateralTable->selectionModel();

        if( !select || !select->hasSelection() || !select->selectedIndexes().size() ){
            showError("You need to select a collateral.");
            return;
        }

        LogPrintf("SmartnodeControlDialog -- use selected collateral\n");

        int row = select->selectedIndexes()[0].row();

        txHash = ui->collateralTable->item(row, COLUMN_TXHASH)->text().toStdString();
        txIndex = ui->collateralTable->item(row, COLUMN_TXID)->text().toStdString();

        QString modeStr;

        LogPrintf("SmartnodeControlDialog -- process request\n");

        if( mode == SmartnodeControlMode::Create ){
            modeStr = tr("created");

            if( !smartnodeConfig.Create(alias, ip, smartnodeKey, txHash, txIndex, strErr) ){
                showError(tr("Could not create smartnode entry:\n\n") +
                          QString::fromStdString(strErr));
                return;
            }
        }else if( mode == SmartnodeControlMode::Edit ){
            modeStr = tr("updated");

            if( !smartnodeConfig.Edit(entryIndex, alias, ip, smartnodeKey, txHash, txIndex, strErr) ){
                showError(tr("Could not edit smartnode entry:\n\n") +
                          QString::fromStdString(strErr));
                return;
            }
        }

        COutPoint collateral(uint256S(txHash), std::atoi(txIndex.c_str()));

        model->lockCoin(collateral);

        QMessageBox::information(this, tr("Success"),
                                       QString::fromStdString(strprintf("Smartnode %s %s!", alias, modeStr.toStdString())),
                                       QMessageBox::Ok);

        done(QDialog::Accepted);
    }else if(!unlockedForEdit.IsNull()){
        model->lockCoin(unlockedForEdit);
    }

    done(QDialog::Rejected);
}

bool SmartnodeControlDialog::validateSmartnodeIPAddress(QString qip){
  std::string ip = qip.toStdString();
  if(ip == "0.0.0.0" || ip == "255.255.255.255" || ip.rfind("10.",0) == 0 || ip.rfind("172.16.",0) == 0 || ip.rfind("192.168.",0) == 0)
    return false;

  return true;
}

void SmartnodeControlDialog::copySmartnodeKey()
{
    GUIUtil::setClipboard(ui->smartnodeKeyLabel->text());
}

void SmartnodeControlDialog::addCustomSmartnodeKey()
{
    QString keyStr = QInputDialog::getText(this,"Custom Smartnode Key","Insert your key here...");

    CKey key;
    CPubKey pubKey;
    if(!CMessageSigner::GetKeysFromSecret(keyStr.toStdString().c_str(), key, pubKey)) {
        QMessageBox::critical(this, tr("Error"),
                                    tr("Invalid Smartnode Key provided\n\n") +
                                    keyStr,
                                    QMessageBox::Ok);
        return;
    }

    ui->smartnodeKeyLabel->setText(keyStr);
}

void SmartnodeControlDialog::sortView(int column, Qt::SortOrder order)
{
    sortColumn = column;
    sortOrder = order;
    ui->collateralTable->sortItems(column, order);
    ui->collateralTable->horizontalHeader()->setSortIndicator(sortColumn, sortOrder);
}

void SmartnodeControlDialog::headerSectionClicked(int logicalIndex)
{

    if (sortColumn == logicalIndex)
        sortOrder = ((sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder);
    else
    {
        sortColumn = logicalIndex;
        sortOrder = Qt::AscendingOrder;
    }

    sortView(sortColumn, sortOrder);
}

void SmartnodeControlDialog::updateView()
{
    if (!model || !model->getOptionsModel() || !model->getAddressTableModel() || !pwalletMain )
        return;

    if( mode == SmartnodeControlMode::Create ){
        CKey secret;
        secret.MakeNewKey(false);
        ui->smartnodeKeyLabel->setText(QString::fromStdString(CBitcoinSecret(secret).ToString()));
    }

    int nRow = 0;
    int nSelectRow = -1;
    QString addressViewStr;
    QString addressLabelViewStr;

    ui->collateralTable->clearContents();
    ui->collateralTable->setRowCount(0);

    std::function<CSmartnodeControlWidgetItem * (QString)> createItem = [](QString title) {
        CSmartnodeControlWidgetItem * item = new CSmartnodeControlWidgetItem(title);
        item->setTextAlignment(Qt::AlignCenter);
        return item;
    };

    std::map<QString, std::vector<COutput> > mapCoins;
    model->listCoins(mapCoins);

    BOOST_FOREACH(const PAIRTYPE(QString, std::vector<COutput>)& coins, mapCoins) {

        QString sWalletAddress = coins.first;
        QString sWalletLabel = model->getAddressTableModel()->labelForAddress(sWalletAddress);
        if (sWalletLabel.isEmpty())
            sWalletLabel = tr("(no label)");

        BOOST_FOREACH(const COutput& out, coins.second) {

            uint256 coinTxHash = out.tx->GetHash();

            if( out.tx->vout[out.i].nValue == 100000 * COIN ){

                if(( coinTxHash.ToString() == this->txHash && this->txIndex == std::to_string(out.i) ) ){
                    nSelectRow = nRow;
                    addressViewStr = sWalletAddress;
                    addressLabelViewStr = sWalletLabel;

                    if( mode == SmartnodeControlMode::Edit ){
                        unlockedForEdit = COutPoint(coinTxHash, out.i);
                        model->unlockCoin(unlockedForEdit);
                    }
                }

                if(model->isLockedCoin(coinTxHash, out.i))
                    continue;

                CTxDestination outputAddress;
                QString sAddress = "";
                if(ExtractDestination(out.tx->vout[out.i].scriptPubKey, outputAddress)){
                    sAddress = QString::fromStdString(CBitcoinAddress(outputAddress).ToString());
                }

                if (!(sAddress == sWalletAddress)){
                    sWalletLabel = tr("(change)");
                }else{
                    QString sLabel = model->getAddressTableModel()->labelForAddress(sAddress);
                    if (sLabel.isEmpty())
                        sWalletLabel = tr("(no label)");
                }

                ui->collateralTable->insertRow(nRow);

                ui->collateralTable->setItem(nRow, COLUMN_LABEL, createItem(sWalletLabel));
                ui->collateralTable->setItem(nRow, COLUMN_ADDRESS, createItem(sAddress));
                ui->collateralTable->setItem(nRow, COLUMN_TXHASH, createItem(QString::fromStdString(coinTxHash.GetHex())));
                ui->collateralTable->setItem(nRow, COLUMN_TXID, createItem(QString::number(out.i)));

                nRow++;
            }
        }
    }

    ui->addressViewLabel->setText(QString("%1 ( %2 )").arg(addressViewStr).arg(addressLabelViewStr));
    ui->txHashViewLabel->setText(QString::fromStdString(txHash));
    ui->txIndexViewLabel->setText(QString::fromStdString(txIndex));

    if( nSelectRow != -1 ){
        ui->collateralTable->selectRow(nSelectRow);
    }

}
