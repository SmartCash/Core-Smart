// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposaladdressdialog.h"
#include "ui_proposaladdressdialog.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "wallet/wallet.h"
#include "init.h"
#include "smartvoting/proposal.h"
#include "smartvoting/voting.h"
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

bool ProposalAddressWidgetItem::operator<(const QTableWidgetItem &other) const {
    int column = other.column();

    if (column == ProposalAddressDialog::COLUMN_AMOUNT){
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

ProposalAddressDialog::ProposalAddressDialog(QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint),
    ui(new Ui::ProposalAddressDialog),
    strAddress(QString())
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
    addressTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    connect(ui->button, SIGNAL(clicked()), this, SLOT(close()));
    connect(ui->addressTable, SIGNAL(itemSelectionChanged()), this, SLOT(itemSelectionChanged()));

    ui->infoLabel->setText(ui->infoLabel->text().arg(CAmountToDouble(SMARTVOTING_PROPOSAL_FEE) + 0.1));

    this->setWindowTitle("Select the proposal address");

    updateUI();
}

ProposalAddressDialog::~ProposalAddressDialog()
{
    delete ui;
}

void ProposalAddressDialog::close()
{
    done(QDialog::Accepted);
}

void ProposalAddressDialog::updateUI()
{

    std::function<ProposalAddressWidgetItem * (QString)> createItem = [](QString title) {
        ProposalAddressWidgetItem * item = new ProposalAddressWidgetItem(title);
        return item;
    };

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::map<std::string,CAmount> mapAddresses;

    map<CTxDestination, CAmount> balances = pwalletMain->GetAddressBalances();

    BOOST_FOREACH(set<CTxDestination> grouping, pwalletMain->GetAddressGroupings())
    {
        BOOST_FOREACH(CTxDestination destination, grouping)
        {

            std::string address = CBitcoinAddress(destination).ToString();

            CAmount& nSum = mapAddresses[address];

            CAmount nAmount = balances[destination];

            nSum += nAmount;

        }
    }

    int nRow = 0;

    QTableWidget *table = ui->addressTable;

    table->clearContents();
    table->setRowCount(0);

    table->setSortingEnabled(false);

    for( auto address : mapAddresses ){

        if( address.second < SMARTVOTING_PROPOSAL_FEE + ( 0.1 * COIN ) )
            continue;

        table->insertRow(nRow);

        table->setItem(nRow, COLUMN_ADDRESS, createItem(QString::fromStdString(address.first)));

        QString amountString = QString::number(CAmountToDouble(address.second),'f',0);

        AddThousandsSpaces(amountString);

        table->setItem(nRow, COLUMN_AMOUNT, createItem(amountString + " SMART"));

        nRow++;
    }

    table->setSortingEnabled(true);
}

void ProposalAddressDialog::itemSelectionChanged()
{
    int nSelectedRow = ui->addressTable->currentRow();
    strAddress = ui->addressTable->item(nSelectedRow, COLUMN_ADDRESS)->text();
}
