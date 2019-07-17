// Copyright (c) 2017 - 2019 - The SmartCash Developers
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
#include "messagesigner.h"
#include "util.h"

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

VoteAddressesDialog::VoteAddressesDialog(const PlatformStyle *platformStyle, SmartVotingManager *votingManager, QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint),
    ui(new Ui::VoteAddressesDialog),
    platformStyle(platformStyle),
    votingManager(votingManager)
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
    addressTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    connect(ui->button, SIGNAL(clicked()), this, SLOT(close()));
    connect(ui->selectionButton, SIGNAL(clicked()),this,SLOT(selectionButtonPressed()));
    connect(ui->addressTable, SIGNAL(cellChanged(int, int)), this, SLOT(cellChanged(int, int)));
    connect(votingManager, SIGNAL(addressesUpdated()), this, SLOT(updateUI()));

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
    LOCK(votingManager->cs_addresses);

    QTableWidgetItem *addressItem = ui->addressTable->item(row,COLUMN_ADDRESS);
    QTableWidgetItem *checkBoxItem = ui->addressTable->item(row,COLUMN_CHECKBOX);

    if( addressItem && checkBoxItem ){

        QString address = addressItem->text();
        bool fChecked = checkBoxItem->checkState() == Qt::Checked;

        auto voteAddress = std::find_if(votingManager->GetAddresses().begin(),
                                        votingManager->GetAddresses().end(),
                                        [address](const SmartVotingAddress& voteAddress) -> bool{
            if( voteAddress.GetAddress() == address ) return true;
            return false;
        });

        if( voteAddress != votingManager->GetAddresses().end() ){
            voteAddress->SetEnabled(fChecked);
        }

    }

    QString votingPowerString = QString::number(std::round(votingManager->GetVotingPower()),'f',0);

    AddThousandsSpaces(votingPowerString);

    ui->votingPowerLabel->setText(votingPowerString + " SMART");
}

void VoteAddressesDialog::updateUI()
{

    LOCK(votingManager->cs_addresses);

    std::function<VoteAddressesWidgetItem * (QString)> createItem = [](QString title) {
        VoteAddressesWidgetItem * item = new VoteAddressesWidgetItem(title);
        return item;
    };

    int nRow = 0;

    QTableWidget *table = ui->addressTable;

    table->clearContents();
    table->setRowCount(0);

    table->setSortingEnabled(false);
    for( auto address : votingManager->GetAddresses() ){

        table->insertRow(nRow);

        VoteAddressesWidgetItem *checkBoxItem = new VoteAddressesWidgetItem();

        checkBoxItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);

        if( address.IsEnabled() ){
            checkBoxItem->setCheckState(Qt::Checked);
        }else{
            checkBoxItem->setCheckState(Qt::Unchecked);
        }

        table->setItem(nRow, COLUMN_CHECKBOX, checkBoxItem);
        table->setItem(nRow, COLUMN_ADDRESS, createItem(address.GetAddress()));

        QString votingPowerString = QString::number(std::round(address.GetVotingPower()),'f',0);

        AddThousandsSpaces(votingPowerString);

        table->setItem(nRow, COLUMN_AMOUNT, createItem(votingPowerString + " SMART"));

        nRow++;
    }
    table->setSortingEnabled(true);

    QString votingPowerString = QString::number(std::round(votingManager->GetVotingPower()),'f',0);

    AddThousandsSpaces(votingPowerString);

    ui->votingPowerLabel->setText(votingPowerString + " SMART");

}

void VoteAddressesDialog::selectionButtonPressed()
{
    LOCK(votingManager->cs_addresses);

    if( votingManager->GetEnabledAddressCount() ){
        for( SmartVotingAddress &address : votingManager->GetAddresses() )
            address.SetEnabled(false);
    }else{
        for( SmartVotingAddress &address : votingManager->GetAddresses() )
            address.SetEnabled(true);
    }

    updateUI();
}
