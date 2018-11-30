// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "voteaddressesdialog.h"
#include "ui_voteaddressesdialog.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "init.h"
#include "smartnode/smartnodeconfig.h"
#include "smartvoting/votevalidation.h"
#include "messagesigner.h"
#include "util.h"
#include "validation.h"

#include <regex>

#include <boost/foreach.hpp>

#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QScrollBar>
#include <QDateTime>
#include <QApplication>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QFlags>
#include <QIcon>
#include <QSettings>
#include <QString>
#include <QRegularExpression>

bool VoteAddressesWidgetItem::operator<(const QTableWidgetItem &other) const {
    int column = other.column();
    if (column == VoteAddressesDialog::COLUMN_AMOUNT){
        QString t1 = text();
        QString t2 = other.text();

        t1 = t1.simplified();
        t1.replace( " ", "" );
        t1.replace("SMART", "");

        t2 = t2.simplified();
        t2.replace( " ", "" );
        t2.replace("SMART", "");

        return t1.toInt() < t2.toInt();

    }else if(column == VoteAddressesDialog::COLUMN_CHECKBOX)
        return checkState() < other.checkState();
    return QTableWidgetItem::operator<(other);
}

VoteAddressesDialog::VoteAddressesDialog(const PlatformStyle *platformStyle, WalletModel *walletModel, QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint),
    ui(new Ui::VoteAddressesDialog),
    platformStyle(platformStyle),
    walletModel(walletModel)
{
    ui->setupUi(this);

    QTableWidget *addressTable = ui->addressTable;

    addressTable->setAlternatingRowColors(true);
    addressTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    addressTable->setSelectionMode(QAbstractItemView::SingleSelection);
    addressTable->setSortingEnabled(true);
    addressTable->setShowGrid(false);
    addressTable->verticalHeader()->hide();

    addressTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    addressTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    addressTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    addressTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    connect(ui->button, SIGNAL(clicked()), this, SLOT(close()));
    connect(ui->selectionButton, SIGNAL(clicked()),this,SLOT(selectionButtonPressed()));
    connect(ui->addressTable, SIGNAL(cellChanged(int, int)), this, SLOT(cellChanged(int, int)));

    this->setWindowTitle("Change your voting power");

    updateUI();
}

VoteAddressesDialog::~VoteAddressesDialog()
{
    delete ui;
}

void VoteAddressesDialog::close()
{
    done(QDialog::Accepted);
}

void VoteAddressesDialog::cellChanged(int row, int column)
{
    if( !pwalletMain ) return;

    QTableWidgetItem *voteKeyItem = ui->addressTable->item(row,COLUMN_KEY);
    QTableWidgetItem *checkBoxItem = ui->addressTable->item(row,COLUMN_CHECKBOX);

    if( voteKeyItem && checkBoxItem ){

        QString voteKey = voteKeyItem->text();

        bool fChecked = checkBoxItem->checkState() == Qt::Checked;

        CVoteKey vk(voteKey.toStdString());
        CKeyID keyId;

        if( vk.GetKeyID(keyId) ){
            LOCK(pwalletMain->cs_wallet);

            CVotingKeyMetadata meta;
            pwalletMain->GetVotingKeyMetadata(keyId, meta);
            meta.fEnabled = fChecked;
            pwalletMain->UpdateVotingKeyMetadata(keyId, meta);
        }
    }

    ui->votingPowerLabel->setText(walletModel->enabledVotingPowerString());
}

void VoteAddressesDialog::updateUI()
{
    if( !pwalletMain || !walletModel ) return;

    std::function<VoteAddressesWidgetItem * (QString)> createItem = [](QString title) {
        VoteAddressesWidgetItem * item = new VoteAddressesWidgetItem(title);
        return item;
    };

    int nRow = 0;
    QTableWidget *table = ui->addressTable;

    std::set<CKeyID> setVotingKeyIds;
    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->GetVotingKeys(setVotingKeyIds);
    }

    table->clearContents();
    table->setRowCount(0);

    table->setSortingEnabled(false);
    for( auto keyId : setVotingKeyIds ){

        CVoteKey voteKey(keyId);
        CVoteKeyValue voteKeyValue;
        CVotingKeyMetadata meta;

        if( !GetVoteKeyValue(voteKey, voteKeyValue) )
            continue;

        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->GetVotingKeyMetadata(keyId, meta);
        }

        table->insertRow(nRow);

        VoteAddressesWidgetItem *checkBoxItem = new VoteAddressesWidgetItem();

        checkBoxItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);

        if( meta.fEnabled ){
            checkBoxItem->setCheckState(Qt::Checked);
        }else{
            checkBoxItem->setCheckState(Qt::Unchecked);
        }

        table->setItem(nRow, COLUMN_CHECKBOX, checkBoxItem);
        table->setItem(nRow, COLUMN_KEY, createItem(QString::fromStdString(voteKey.ToString())));
        table->setItem(nRow, COLUMN_ADDRESS, createItem(QString::fromStdString(voteKeyValue.voteAddress.ToString())));
        table->setItem(nRow, COLUMN_AMOUNT, createItem(walletModel->votingPowerString(voteKey)));

        nRow++;
    }
    table->setSortingEnabled(true);

    ui->votingPowerLabel->setText(walletModel->enabledVotingPowerString());

}

void VoteAddressesDialog::selectionButtonPressed()
{
    if( !walletModel ) return;

    if( walletModel->enabledVoteKeys() ){
        walletModel->updateVoteKeys(false);
    }else{
        walletModel->updateVoteKeys(true);
    }

    updateUI();
}
