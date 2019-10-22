// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addressconverter.h"
#include "ui_addressconverter.h"

#include "bitcoinunits.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "txmempool.h"
#include "walletmodel.h"
#include "sendcoinsdialog.h"

#include "wallet/wallet.h"

#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QString>


AddressConverter::AddressConverter(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AddressConverter)
{
    ui->setupUi(this);

    connect(ui->input, SIGNAL(textChanged(QString)), this, SLOT(addressInputChanged()));
}

AddressConverter::~AddressConverter()
{
    delete ui;
}

void AddressConverter::addressInputChanged()
{
    if( ui->input->text() == "" ){
        ui->output->setText("");
        return;
    }

    CSmartAddress address(ui->input->text().toStdString());

    if(address.IsValid(CChainParams::PUBKEY_ADDRESS) || address.IsValid(CChainParams::SCRIPT_ADDRESS)){
        ui->output->setText(QString::fromStdString(address.ToString(true)));
    }else if(address.IsValid(CChainParams::PUBKEY_ADDRESS_V2) || address.IsValid(CChainParams::SCRIPT_ADDRESS_V2)){
        ui->output->setText(QString::fromStdString(address.ToString(false)));
    }else{
        ui->output->setText("Invalid SmartCash address");
    }

}
