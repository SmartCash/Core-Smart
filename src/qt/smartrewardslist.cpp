#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "smartrewardslist.h"
#include "ui_smartrewardslist.h"

#include "smartrewards/rewards.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "txmempool.h"
#include "walletmodel.h"
#include "coincontrol.h"
#include "smartrewardslist.h"
//#include "init.h"
//#include "validation.h" // For minRelayTxFee
#include "wallet/wallet.h"
#include "clientmodel.h"

#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableWidget>
#include <QDateTime>

struct QSmartRewardField
{
    QString label;
    QString address;
    CAmount balance;
    CAmount eligible;
    CAmount reward;

    QSmartRewardField() : label(QString()), address(QString()),
                          balance(0), eligible(0),reward(0){}
};

SmartrewardsList::SmartrewardsList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SmartrewardsList),
    model(0)
{
    ui->setupUi(this);

    QTableWidget *smartRewardsTable = ui->tableWidget;

    smartRewardsTable->setAlternatingRowColors(true);
    smartRewardsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    smartRewardsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    smartRewardsTable->setSortingEnabled(true);
    smartRewardsTable->setShowGrid(false);
    smartRewardsTable->verticalHeader()->hide();

    smartRewardsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    smartRewardsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    smartRewardsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    smartRewardsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    smartRewardsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    WaitingSpinnerWidget * spinner = ui->spinnerWidget;

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

    // Actions
    smartRewardsTable->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction *copyAddressAction = new QAction(tr("Copy address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
    QAction *copyEligibleAmountAction = new QAction(tr("Copy eligible amount"), this);
    QAction *copyRewardAction = new QAction(tr("Copy expected reward"), this);

    contextMenu = new QMenu(this);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyEligibleAmountAction);
    contextMenu->addAction(copyRewardAction);

    // Connect actions
    connect(smartRewardsTable, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyEligibleAmountAction, SIGNAL(triggered()), this, SLOT(copyEligibleAmount()));
    connect(copyRewardAction, SIGNAL(triggered()), this, SLOT(copyReward()));

    ui->stackedWidget->setCurrentIndex(0);
}

SmartrewardsList::~SmartrewardsList()
{
    delete ui;
}

void SmartrewardsList::setModel(WalletModel *model)
{
    this->model = model;
    updateUI();
}

void SmartrewardsList::setClientModel(ClientModel *model)
{
    this->clientModel = model;

    if( clientModel )
        connect(clientModel, SIGNAL(SmartRewardsUpdated()), this, SLOT(updateUI()));

}

void SmartrewardsList::contextualMenu(const QPoint &point)
{
    QModelIndex index =  ui->tableWidget->indexAt(point);
    QModelIndexList selection =  ui->tableWidget->selectionModel()->selectedRows(0);
    if (selection.empty())
        return;

    if(index.isValid())
    {
        contextMenu->exec(QCursor::pos());
    }
}

void SmartrewardsList::copyLabel()
{
    GUIUtil::copyEntryData(ui->tableWidget, 0);
}


void SmartrewardsList::copyAddress()
{
    GUIUtil::copyEntryData(ui->tableWidget, 1);
}


void SmartrewardsList::copyAmount()
{
    GUIUtil::copyEntryData(ui->tableWidget, 2);
}

void SmartrewardsList::copyEligibleAmount()
{
    GUIUtil::copyEntryData(ui->tableWidget, 3);
}

void SmartrewardsList::copyReward()
{
    GUIUtil::copyEntryData(ui->tableWidget, 4);
}

void SmartrewardsList::updateUI()
{
    static int64_t lastUpdate = 0;
    // If the wallet model hasn't been set yet we cant update the UI.
    if(!model) {
        return;
    }

    int64_t currentTime = QDateTime::currentMSecsSinceEpoch() / 1000;

    if( !lastUpdate || currentTime - lastUpdate  > 10 ){
        lastUpdate = currentTime;
    }else{
        return;
    }

    ui->spinnerWidget->stop();

    // If the rewardlist is synced show the actual SmartRewards view.
    if( prewards->IsSynced() ){

        CSmartRewardRound current;
        {
            LOCK(cs_rewardrounds);
            current = prewards->GetCurrentRound();
        }
        QString percentText;
        percentText.sprintf("%.2f%%", current.percent * 100);
        ui->percentLabel->setText(percentText);

        ui->roundLabel->setText(QString::number(current.number));

        QDateTime roundEnd;
        roundEnd.setTime_t(current.endBlockTime);
        QString roundEndText = roundEnd.toString(Qt::SystemLocaleShortDate);

        if( current.endBlockTime < currentTime ) {
            roundEndText += " ( Now )";
        }else{
            uint64_t minutesLeft = ( (uint64_t)current.endBlockTime - currentTime ) / 60;
            uint64_t days = minutesLeft / 1440;
            uint64_t hours = (minutesLeft % 1440) / 60;
            uint64_t minutes = (minutesLeft % 1440) % 60;

            roundEndText += " ( ";
            if( days ){
                roundEndText += QString("%1day%2").arg(days).arg(days > 1 ? "s":"");
            }

            if( hours ){
                if( days ) roundEndText += ", ";
                roundEndText += QString("%1hour%2").arg(hours).arg(hours > 1 ? "s":"");
            }

            if( !days && minutes ){
                if( !hours ) roundEndText += ", ";
                roundEndText += QString("%1minute%2").arg(minutes).arg(minutes > 1 ? "s":"");
            }

            roundEndText += " )";
        }

        ui->nextRoundLabel->setText(roundEndText);

        int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

        std::map<QString, std::vector<COutput> > mapCoins;
        model->listCoins(mapCoins);

        std::vector<QSmartRewardField> rewardList;

        BOOST_FOREACH(const PAIRTYPE(QString, std::vector<COutput>)& coins, mapCoins) {

            QString sWalletAddress = coins.first;
            QString sWalletLabel = model->getAddressTableModel()->labelForAddress(sWalletAddress);

            if (sWalletLabel.isEmpty())
                sWalletLabel = tr("(no label)");

            QSmartRewardField rewardField;

            rewardField.address = sWalletAddress;
            rewardField.label = sWalletLabel;

            BOOST_FOREACH(const COutput& out, coins.second) {

                CTxDestination outputAddress;
                QString sAddress;

                if(ExtractDestination(out.tx->vout[out.i].scriptPubKey, outputAddress)){

                    sAddress = QString::fromStdString(CBitcoinAddress(outputAddress).ToString());

                    if (!(sAddress == sWalletAddress)){ // change address

                        QSmartRewardField change;
                        CSmartRewardEntry reward;

                        change.address = sAddress;
                        change.label = tr("(change)");
                        change.balance = out.tx->vout[out.i].nValue;

                        if( prewards->GetRewardEntry(CSmartAddress(sAddress.toStdString()),reward) ){
                            change.balance = reward.balance;
                            change.eligible = reward.eligible ? reward.balanceOnStart : 0;
                            change.reward = current.percent * change.eligible;
                        }

                        if( change.balance ) rewardList.push_back(change);

                        continue;

                    }else{

                        rewardField.label = model->getAddressTableModel()->labelForAddress(rewardField.address);

                        if (rewardField.label.isEmpty())
                            rewardField.label = tr("(no label)");
                    }

                }

            }

            if( !rewardField.address.isEmpty() ){

                CSmartRewardEntry reward;

                if( prewards->GetRewardEntry(CSmartAddress(rewardField.address.toStdString()),reward) ){
                    rewardField.balance = reward.balance;
                    rewardField.eligible = reward.eligible ? reward.balanceOnStart : 0;
                    rewardField.reward = current.percent * rewardField.eligible;
                }

                if( rewardField.balance ) rewardList.push_back(rewardField);
            }
        }

        int nRow = 0;

        CAmount rewardSum = 0;

        ui->tableWidget->clearContents();
        ui->tableWidget->setRowCount(0);

        std::function<QTableWidgetItem * (QString)> createItem = [](QString title) {
            QTableWidgetItem * item = new QTableWidgetItem(title);
            item->setTextAlignment(Qt::AlignCenter);
            return item;
        };

        BOOST_FOREACH(const QSmartRewardField& field, rewardList) {

            ui->tableWidget->insertRow(nRow);

            ui->tableWidget->setItem(nRow, 0, createItem(field.label));
            ui->tableWidget->setItem(nRow, 1, createItem(field.address));
            ui->tableWidget->setItem(nRow, 2, createItem(BitcoinUnits::format(nDisplayUnit, field.balance) + " " +  BitcoinUnits::name(nDisplayUnit)));
            ui->tableWidget->setItem(nRow, 3, createItem(BitcoinUnits::format(nDisplayUnit, field.eligible) + " " +  BitcoinUnits::name(nDisplayUnit)));
            ui->tableWidget->setItem(nRow, 4, createItem(BitcoinUnits::format(nDisplayUnit, field.reward) + " " +  BitcoinUnits::name(nDisplayUnit)));

            nRow++;
            rewardSum += field.reward;
        }

        ui->sumLabel->setText(BitcoinUnits::format(nDisplayUnit, rewardSum) + " " +  BitcoinUnits::name(nDisplayUnit));

        if( ui->stackedWidget->currentIndex() != 2) ui->stackedWidget->setCurrentIndex(2);

    }else{
        double progress = prewards->GetProgress() * ui->loadingProgress->maximum();
        ui->loadingProgress->setValue(progress);

        // If not show the loading view.
        if( ui->stackedWidget->currentIndex() != 1) ui->stackedWidget->setCurrentIndex(1);
    }

}
