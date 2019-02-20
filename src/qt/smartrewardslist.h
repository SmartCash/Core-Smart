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

namespace Ui {
    class SmartrewardsList;
}

class CSmartRewardWidgetItem : public QTableWidgetItem
{
public:
    CSmartRewardWidgetItem(const QString &text, int type = Type) : QTableWidgetItem(text, type) {}
    bool operator<(const QTableWidgetItem &other) const;
};

class WalletModel;
class ClientModel;
class OptionsModel;
class PlatformStyle;
class QModelIndex;

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

public:
    explicit SmartrewardsList(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~SmartrewardsList();

    void setModel(WalletModel *model);
    void setClientModel(ClientModel *model);

    enum
    {
        COLUMN_LABEL = 0,
        COLUMN_ADDRESS,
        COLUMN_AMOUNT,
        COLUMN_ELIGIBLE,
        COLUMN_REWARD,
    };

private:
    Ui::SmartrewardsList *ui;
    WalletModel *model;
    ClientModel *clientModel;
    QMenu *contextMenu;
    

public Q_SLOTS:
    void contextualMenu(const QPoint &);
    void copyAddress();
    void copyLabel();
    void copyAmount();
    void copyEligibleAmount();
    void copyReward();
    void updateUI();
};
#endif // SMARTREWARDSLIST_H
