// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_QT_SPECIALTRANSACTIONDIALOG_H
#define SMARTCASH_QT_SPECIALTRANSACTIONDIALOG_H

#include "amount.h"

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTreeWidgetItem>

class PlatformStyle;
class WalletModel;
class CCoinControlWidgetItem;

class COutPoint;
class CTxMemPool;
class CWalletTx;


namespace Ui {
    class SpecialTransactionDialog;
}

enum SpecialTransactionType {
    REGISTRATION_TRANSACTIONS,
    ACTIVATION_TRANSACTIONS
};

#define ASYMP_UTF8 "\xE2\x89\x88"

class SpecialTransactionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SpecialTransactionDialog(const SpecialTransactionType type, const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~SpecialTransactionDialog();

    void setModel(WalletModel *model);

    std::map<QString, COutPoint> mapOutputs;

private:
    Ui::SpecialTransactionDialog *ui;
    WalletModel *model;
    SpecialTransactionType type;
    int sortColumn;
    Qt::SortOrder sortOrder;
    CAmount nRequiredFee;
    CAmount nRequiredNetworkFee;

    QMenu *contextMenu;
    QTreeWidgetItem *contextMenuItem;

    const PlatformStyle *platformStyle;

    void UpdateElements();
    CAmount GetRequiredTotal() { return nRequiredFee + nRequiredNetworkFee; }

    void sortView(int, Qt::SortOrder);
    void updateView();
    void selectSmallestOutput(QTreeWidgetItem* topLevel);

    void SendTransactions(std::vector<QString> &vecErrors);
    bool SendRegistration(const QString &address, const COutPoint &out, QString &strError);
    bool SendActivationTransaction(const QString &address, const COutPoint &out, int nCurrentRound, QString &strError);

    enum
    {
        COLUMN_CHECKBOX = 0,
        COLUMN_AMOUNT,
        COLUMN_LABEL,
        COLUMN_ADDRESS,
        COLUMN_TXHASH,
        COLUMN_VOUT_INDEX
    };
    friend class CCoinControlWidgetItem;

private Q_SLOTS:
    void showMenu(const QPoint &);
    void copyAddress();
    void viewItemChanged(QTreeWidgetItem*, int);
    void headerSectionClicked(int);
    void buttonBoxClicked(QAbstractButton*);
    void buttonSelectAllClicked();
};

static const QString strRegistrationTitle = "Register VoteKeys";
static const QString strRegistrationDescription = (
"Use this form to register your SmartCash addresses "
"for the SmartVoting system. By doing this you will get a VoteKey for every "
"address you register. A VoteKey allows you vote on proposals with the associated "
"address without the need to expose the private key of it."
);
static const QString strRegistrationFeeDescription = "Register fee";

static const QString strActivationTxTitle = "Activate Rewards";
static const QString strActivationTxDescription = (
"Use this form to send an ActivateReward transaction to make your addresses eligible for SmartRewards. "
"A small fee of 0.001 SMART will be taken from outputs you choose.\n\n"
"You can either manually select an input for each address or automatically select the smallest input for each address by clicking the checkbox below."
);

#endif // SMARTCASH_QT_SPECIALTRANSACTIONDIALOG_H
