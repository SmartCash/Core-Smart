// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_QT_ADDRESSCONVERTER_H
#define SMARTCASH_QT_ADDRESSCONVERTER_H

#include <QDialog>

namespace Ui {
    class AddressConverter;
}

class AddressConverter : public QDialog
{
    Q_OBJECT

public:
    explicit AddressConverter(QWidget *parent = 0);
    ~AddressConverter();

private:
    Ui::AddressConverter *ui;

private Q_SLOTS:
    void addressInputChanged();

};

#endif // SMARTCASH_QT_ADDRESSCONVERTER_H
