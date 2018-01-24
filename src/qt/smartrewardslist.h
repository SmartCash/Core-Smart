// Copyright (c) 2011-2015 The Smartcash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_SMARTREWARDSLIST_H
#define BITCOIN_QT_SMARTREWARDSLIST_H

#include "primitives/transaction.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define SMARTREWARDS_DAY                                 25
#define SMARTREWARDS_UTC_HOUR                            07
#define SMARTREWARDS_MINIMUM_AMOUNT                    1000

namespace Ui {
    class SmartrewardsList;
}

class WalletModel;
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

private:
    QMenu *contextMenu;

public Q_SLOTS:
    void copyAddress();
    void copyLabel();
    void copyAmount();
    void copyEligibleAmount();
    void updateRewardsList();

Q_SIGNALS:

private:
    QTimer *timer;
    Ui::SmartrewardsList *ui;
    WalletModel *model;

private Q_SLOTS:
    void contextualMenu(const QPoint &);
    void on_updatePushButton_clicked();
};
#endif // SMARTREWARDSLIST_H
