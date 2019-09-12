// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_QT_VOTEADDRESSESDIALOG_H
#define SMARTCASH_QT_VOTEADDRESSESDIALOG_H

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTableWidgetItem>

#include "primitives/transaction.h"
#include "smartproposal.h"

class PlatformStyle;

namespace Ui {
    class VoteAddressesDialog;
}

class VoteAddressesWidgetItem : public QTableWidgetItem
{
public:
    VoteAddressesWidgetItem() : QTableWidgetItem() {}
    VoteAddressesWidgetItem(const QString &text, int type = Type) : QTableWidgetItem(text, type) {}
    bool operator<(const QTableWidgetItem &other) const;
};

class VoteAddressesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VoteAddressesDialog(const PlatformStyle *platformStyle, SmartVotingManager *votingManager, QWidget *parent = 0);
    ~VoteAddressesDialog();

    enum{
        COLUMN_CHECKBOX,
        COLUMN_AMOUNT,
        COLUMN_ADDRESS
    };

private:



    Ui::VoteAddressesDialog *ui;

    const PlatformStyle *platformStyle;

    SmartVotingManager * votingManager;
    std::map<std::string,CAmount> vecAddresses;

private Q_SLOTS:
    void close();
    void updateUI();
    void selectionButtonPressed();
    void cellChanged(int row, int column);
};

#endif // SMARTCASH_QT_CASTVOTESDIALOG_H
