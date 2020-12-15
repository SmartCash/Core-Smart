// Copyright (c) 2017 - 2020 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "termrewardslist.h"
#include "ui_termrewardslist.h"

#include "validation.h"
#include "smartrewards/rewards.h"

#include <QDateTime>

TermRewardsList::TermRewardsList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TermRewardsList),
    clientModel(nullptr),
    model(nullptr),
    state(STATE_INIT)
{
    ui->setupUi(this);

    WaitingSpinnerWidget *spinner = ui->spinnerWidget;
    spinner->setRoundness(70.0);
    spinner->setMinimumTrailOpacity(15.0);
    spinner->setTrailFadePercentage(70.0);
    spinner->setNumberOfLines(14);
    spinner->setLineLength(14);
    spinner->setLineWidth(6);
    spinner->setInnerRadius(20);
    spinner->setRevolutionsPerSecond(1);
    spinner->setColor(QColor(254, 198, 13));

    spinner->start();

    ui->tableWidgetTermRewards->setColumnWidth(COLUMN_ADDRESS, 300);
    ui->tableWidgetTermRewards->setColumnWidth(COLUMN_TX_ID, 500);
    ui->tableWidgetTermRewards->setColumnWidth(COLUMN_BALANCE, 150);
    ui->tableWidgetTermRewards->setColumnWidth(COLUMN_LEVEL, 80);
    ui->tableWidgetTermRewards->setColumnWidth(COLUMN_APY, 80);
    ui->tableWidgetTermRewards->setColumnWidth(COLUMN_EXPIRATION, 150);
}

TermRewardsList::~TermRewardsList()
{
    delete ui;
}

void TermRewardsList::setState(TermRewardsList::TermRewardsListState state)
{
    this->state = state;
    updateUI();
}

void TermRewardsList::setModel(WalletModel *model)
{
    this->model = model;
    updateUI();
}

void TermRewardsList::setClientModel(ClientModel *model)
{
    this->clientModel = model;

    if (clientModel) {
        connect(clientModel, SIGNAL(TermRewardsUpdated()), this, SLOT(updateUI()));
    }
}

void TermRewardsList::updateUI()
{
    if (!model) {
        return;
    }

    switch(state){
    case STATE_INIT:
        if (prewards->IsSynced() && !fReindex) {
            ui->spinnerWidget->stop();
            setState(STATE_OVERVIEW);
        }

        break;
    case STATE_OVERVIEW:
        updateOverviewUI();
        break;
    default:
        break;
    }

    if (ui->stackedWidget->currentIndex() != state) {
        ui->stackedWidget->setCurrentIndex(state);
    }
}

void TermRewardsList::updateOverviewUI()
{
    CTermRewardEntryMap entries;

    {
        TRY_LOCK(cs_rewardscache, cacheLocked);

        if(!cacheLocked) {
            return;
        }

        if (!prewards->GetTermRewardsEntries(entries)) {
            return;
        }
    }

    for (const auto &entry : entries) {
        CTermRewardEntry *reward = entry.second;
        if (model->isMine(CSmartAddress(reward->GetAddress()).Get())) {
            bool fOldRowFound = false;
            int nNewRow = 0;

            for(int i = 0; i < ui->tableWidgetTermRewards->rowCount(); i++) {
                if (ui->tableWidgetTermRewards->item(i, COLUMN_ADDRESS)->text().toStdString() == reward->GetAddress()
                 && ui->tableWidgetTermRewards->item(i, COLUMN_TX_ID)->text().toStdString() == reward->txHash.GetHex()) {
                    fOldRowFound = true;
                    nNewRow = i;
                    break;
                }
            }

            if( nNewRow == 0 && !fOldRowFound) {
                nNewRow = ui->tableWidgetTermRewards->rowCount();
                ui->tableWidgetTermRewards->insertRow(nNewRow);
            }

            QString expirationDate = QDateTime::fromTime_t(reward->expires).toString("MM.dd.yyyy");
            ui->tableWidgetTermRewards->setItem(nNewRow, COLUMN_ADDRESS, new TermRewardsWidgetItem(reward->GetAddress()));
            ui->tableWidgetTermRewards->setItem(nNewRow, COLUMN_TX_ID, new TermRewardsWidgetItem(reward->txHash.GetHex()));
            ui->tableWidgetTermRewards->setItem(nNewRow, COLUMN_BALANCE, new TermRewardsWidgetItem(reward->balance));
            ui->tableWidgetTermRewards->setItem(nNewRow, COLUMN_LEVEL, new TermRewardsWidgetItem(reward->GetLevel()));
            ui->tableWidgetTermRewards->setItem(nNewRow, COLUMN_APY, new TermRewardsWidgetItem(reward->percent));
            ui->tableWidgetTermRewards->setItem(nNewRow, COLUMN_EXPIRATION, new TermRewardsWidgetItem(expirationDate));
        }
    }
}
