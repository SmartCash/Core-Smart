// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "editaddressdialog.h"
#include "ui_editaddressdialog.h"

#include "addresstablemodel.h"
#include "guiutil.h"
#include "validation.h"
#include "chainparams.h"

#include <QDataWidgetMapper>
#include <QMessageBox>

#define ONE_MONTH                     (30.5 * 24 * 60 * 60)
#define ONE_YEAR                      (365 * 24 * 60 * 60)

EditAddressDialog::EditAddressDialog(Mode mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditAddressDialog),
    mapper(0),
    mode(mode),
    model(0),
    nLockTime(0)
{
    ui->setupUi(this);

    GUIUtil::setupAddressWidget(ui->addressEdit, this);

    switch(mode)
    {
    case NewReceivingAddress:
        setWindowTitle(tr("New receiving address"));
        ui->addressEdit->setEnabled(false);
        break;
    case NewSendingAddress:
        setWindowTitle(tr("New sending address"));
        break;
    case EditReceivingAddress:
        setWindowTitle(tr("Edit receiving address"));
        ui->addressEdit->setEnabled(false);
        break;
    case EditSendingAddress:
        setWindowTitle(tr("Edit sending address"));
        break;
    }

    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);

    // Timelock
    const int nAvgBlockTime = Params().GetConsensus().nPowTargetSpacing;
    timeLockItems.emplace_back("Set LockTime", 0);
    timeLockItems.emplace_back("1 month", (int)(ONE_MONTH / nAvgBlockTime));
    timeLockItems.emplace_back("2 months", (int)(2 * ONE_MONTH / nAvgBlockTime));
    timeLockItems.emplace_back("3 months", (int)(3 * ONE_MONTH / nAvgBlockTime));
    timeLockItems.emplace_back("6 months", (int)(6 * ONE_MONTH / nAvgBlockTime));
    timeLockItems.emplace_back("1 year", (int)(ONE_YEAR / nAvgBlockTime));
    timeLockItems.emplace_back("Custom (until block)", -1);
    timeLockItems.emplace_back("Custom (until date)", -1);
    for (const auto &i : timeLockItems) {
        ui->timelockCombo->addItem(i.first);
    }

    ui->timeLockCustomBlocks->setVisible(false);
    ui->timeLockCustomBlocks->setRange(1, 1000000);
    ui->timeLockCustomDate->setVisible(false);
    ui->timeLockCustomDate->setMinimumDateTime(QDateTime::currentDateTime());
    connect(ui->timeLockCustomBlocks, SIGNAL(valueChanged(int)), this, SLOT(timeLockCustomBlocksChanged(int)));
    connect(ui->timeLockCustomDate, SIGNAL(dateTimeChanged(const QDateTime&)), this,
        SLOT(timeLockCustomDateChanged(const QDateTime&)));
    connect(ui->timelockCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(timelockComboChanged(int)));

    // Make Timelock feature visible only if supermajority enforced BIP65
    if(!IsSuperMajority(4, chainActive.Tip(), Params().GetConsensus().nMajorityEnforceBlockUpgrade,
          Params().GetConsensus()))
    {
        ui->timelockCombo->setVisible(false);
    }
}

EditAddressDialog::~EditAddressDialog()
{
    delete ui;
}

void EditAddressDialog::setModel(AddressTableModel *model)
{
    this->model = model;
    if(!model)
        return;

    mapper->setModel(model);
    mapper->addMapping(ui->labelEdit, AddressTableModel::Label);
    mapper->addMapping(ui->addressEdit, AddressTableModel::Address);
}

void EditAddressDialog::loadRow(int row)
{
    mapper->setCurrentIndex(row);
}

bool EditAddressDialog::saveCurrentRow()
{
    if(!model)
        return false;

    switch(mode)
    {
    case NewReceivingAddress:
    case NewSendingAddress:
        address = model->addRow(
                mode == NewSendingAddress ? AddressTableModel::Send : AddressTableModel::Receive,
                ui->labelEdit->text(),
                ui->addressEdit->text(),
                nLockTime);
        break;
    case EditReceivingAddress:
    case EditSendingAddress:
        if(mapper->submit())
        {
            address = ui->addressEdit->text();
        }
        break;
    }
    return !address.isEmpty();
}

void EditAddressDialog::accept()
{
    if(!model)
        return;

    if(!saveCurrentRow())
    {
        switch(model->getEditStatus())
        {
        case AddressTableModel::OK:
            // Failed with unknown reason. Just reject.
            break;
        case AddressTableModel::NO_CHANGES:
            // No changes were made during edit operation. Just reject.
            break;
        case AddressTableModel::INVALID_ADDRESS:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered address \"%1\" is not a valid SmartCash address.").arg(ui->addressEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case AddressTableModel::DUPLICATE_ADDRESS:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered address \"%1\" is already in the address book.").arg(ui->addressEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case AddressTableModel::WALLET_UNLOCK_FAILURE:
            QMessageBox::critical(this, windowTitle(),
                tr("Could not unlock wallet."),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case AddressTableModel::KEY_GENERATION_FAILURE:
            QMessageBox::critical(this, windowTitle(),
                tr("New key generation failed."),
                QMessageBox::Ok, QMessageBox::Ok);
            break;

        }
        return;
    }
    QDialog::accept();
}

QString EditAddressDialog::getAddress() const
{
    return address;
}

void EditAddressDialog::setAddress(const QString &address)
{
    this->address = address;
    ui->addressEdit->setText(address);
}

void EditAddressDialog::timelockComboChanged(int index)
{
    if (timeLockItems[index].first == "Custom (until block)") {
        ui->timeLockCustomDate->setVisible(false);
        ui->timeLockCustomBlocks->setVisible(true);
        nLockTime = ui->timeLockCustomBlocks->value();
    }
    else if (timeLockItems[index].first == "Custom (until date)")
    {
        ui->timeLockCustomDate->setVisible(true);
        ui->timeLockCustomBlocks->setVisible(false);
        nLockTime = ui->timeLockCustomDate->dateTime().toMSecsSinceEpoch() / 1000;
    }
    else
    {
        ui->timeLockCustomDate->setVisible(false);
        ui->timeLockCustomBlocks->setVisible(false);
        nLockTime = timeLockItems[index].second > 0 ? chainActive.Height() + timeLockItems[index].second : 0;
    }
}

void EditAddressDialog::timeLockCustomBlocksChanged(int i)
{
    nLockTime = i;
}

void EditAddressDialog::timeLockCustomDateChanged(const QDateTime &dt)
{
    nLockTime = dt.toMSecsSinceEpoch() / 1000;
}

