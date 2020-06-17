// Copyright (c) 2017-2019 The SmartCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_SMARTREWARDSLIST_H
#define BITCOIN_QT_SMARTREWARDSLIST_H

#include "primitives/transaction.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QDialog>
#include <QMenu>
#include <QTimer>
#include <QWidget>
#include <QTableWidgetItem>

class CSmartRewardRound;
class CBlockIndex;

namespace Ui {
    class SmartrewardsList;
}

class CSmartRewardWidgetItem : public QTableWidgetItem
{
public:
    CSmartRewardWidgetItem(const QString &text, int type = Type) : QTableWidgetItem(text, type) {}
    bool operator<(const QTableWidgetItem &other) const;
};

class CSmartRewardVoteProofWidgetItem : public QTableWidgetItem
{
public:
    CSmartRewardVoteProofWidgetItem(const QString &text, int type = Type) : QTableWidgetItem(text, type) {}
    bool operator<(const QTableWidgetItem &other) const;
};

class WalletModel;
class ClientModel;
class OptionsModel;
class PlatformStyle;
class QModelIndex;
class QSmartRewardEntry;

QT_BEGIN_NAMESPACE
class QItemSelection;
class QMenu;
class QModelIndex;
class QSortFilterProxyModel;
class QTableView;
QT_END_NAMESPACE

/** SmartrewardsList Manager page widget */
class SmartrewardsList : public QWidget
{
    Q_OBJECT

    enum SmartRewardsListState{
        STATE_INIT,
        STATE_OVERVIEW
    };

    Ui::SmartrewardsList *ui;
    WalletModel *model;
    ClientModel *clientModel;
    const PlatformStyle *platformStyle;
    std::vector<QSmartRewardEntry*> vecEntries;
    std::vector<QWidget*> vecLines;
    SmartRewardsListState state;

    void setState(SmartrewardsList::SmartRewardsListState state);

public:
    explicit SmartrewardsList(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~SmartrewardsList();

    void setModel(WalletModel *model);
    void setClientModel(ClientModel *model);

    enum OverviewColummns
    {
        COLUMN_LABEL = 0,
        COLUMN_ADDRESS,
        COLUMN_AMOUNT,
        COLUMN_ELIGIBLE,
        COLUMN_REWARD,
    };

public Q_SLOTS:
    void updateOverviewUI(const CSmartRewardRound &currentRound, const CBlockIndex *tip);
    void updateUI();

    void on_btnSendProofs_clicked();

    void scrollChanged(int value);
};
#endif // SMARTREWARDSLIST_H
