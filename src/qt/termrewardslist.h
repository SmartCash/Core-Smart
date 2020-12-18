// Copyright (c) 2017 - 2020 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TERMREWARDSLIST_H
#define TERMREWARDSLIST_H

#include "platformstyle.h"
#include "clientmodel.h"
#include "walletmodel.h"

#include <QWidget>
#include <QTableWidgetItem>

namespace Ui {
    class TermRewardsList;
}

class TermRewardsWidgetItem : public QTableWidgetItem
{
public:
    TermRewardsWidgetItem(const QString &title) : QTableWidgetItem(title, Type) {}
    TermRewardsWidgetItem(const std::string &title) : QTableWidgetItem(QString::fromStdString(title), Type) {}
    TermRewardsWidgetItem(uint8_t title) : QTableWidgetItem(QString::number(title), Type) {}
    TermRewardsWidgetItem(CAmount title) : QTableWidgetItem(QString::number(title / COIN), Type) {}
    TermRewardsWidgetItem(int title) : QTableWidgetItem(QString::number(title), Type) {}
};

class TermRewardsList : public QWidget
{
    Q_OBJECT

    enum TermRewardsListState{
        STATE_INIT,
        STATE_OVERVIEW
    };

    TermRewardsListState state;
    Ui::TermRewardsList *ui;
    WalletModel *model;
    ClientModel *clientModel;

    void setState(TermRewardsList::TermRewardsListState state);
    void updateOverviewUI();

public:
    explicit TermRewardsList(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~TermRewardsList();

    void setModel(WalletModel *model);
    void setClientModel(ClientModel *model);

    enum{
        COLUMN_ADDRESS = 0,
        COLUMN_BALANCE,
        COLUMN_LEVEL,
        COLUMN_APY,
        COLUMN_EXPIRATION,
        COLUMN_TX_ID
    };

public Q_SLOTS:
    void updateUI();
};

#endif // TERMREWARDSLIST_H

