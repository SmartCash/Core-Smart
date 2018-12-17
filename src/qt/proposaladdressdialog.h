// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_QT_PROPOSALADDRESSDIALOG_H
#define SMARTCASH_QT_PROPOSALADDRESSDIALOG_H

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTableWidgetItem>

#include "primitives/transaction.h"
#include "proposaladdressdialog.h"

class PlatformStyle;

namespace Ui {
    class ProposalAddressDialog;
}

class ProposalAddressWidgetItem : public QTableWidgetItem
{
public:
    ProposalAddressWidgetItem() : QTableWidgetItem() {}
    ProposalAddressWidgetItem(const QString &text, int type = Type) : QTableWidgetItem(text, type) {}
    bool operator<(const QTableWidgetItem &other) const;
};

class ProposalAddressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProposalAddressDialog(QWidget *parent = 0);
    ~ProposalAddressDialog();

    enum{
        COLUMN_ADDRESS,
        COLUMN_AMOUNT
    };

    QString GetAddress() { return strAddress; }

private:

    Ui::ProposalAddressDialog *ui;

    QString strAddress;

private Q_SLOTS:
    void close();
    void updateUI();
    void itemSelectionChanged();
};

#endif // SMARTCASH_QT_PROPOSALADDRESSDIALOG_H
