#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif
 
#include "smartrewardslist.h"
#include "ui_smartrewardslist.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "txmempool.h"
#include "walletmodel.h"
#include "coincontrol.h"
#include "init.h"
#include "sync.h"
#include "main.h"
#include "wallet/wallet.h"
 
#include <boost/assign/list_of.hpp> // for 'map_list_of()'
 
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableWidget>
#include <QTime>

SmartrewardsList::SmartrewardsList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SmartrewardsList),
    model(0)
{
    ui->setupUi(this);
 
   //QMessageBox::information(this,"Hello","test");

   QTableWidget *smartRewardsTable = ui->tableWidget;
   //QTableWidget *smartRewardsTable = new QTableWidget(this);

   smartRewardsTable->setAlternatingRowColors(true);
   smartRewardsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
   smartRewardsTable->setSelectionMode(QAbstractItemView::SingleSelection);
   smartRewardsTable->setSortingEnabled(true);
   smartRewardsTable->setColumnCount(4);
   smartRewardsTable->setShowGrid(false);
   smartRewardsTable->verticalHeader()->hide();

   smartRewardsTable->setColumnWidth(0, 200);
   smartRewardsTable->setColumnWidth(1, 250);
   smartRewardsTable->setColumnWidth(2, 160);
   smartRewardsTable->setColumnWidth(3, 200);

   // Actions
   smartRewardsTable->setContextMenuPolicy(Qt::CustomContextMenu);

   QAction *copyAddressAction = new QAction(tr("Copy address"), this);
   QAction *copyLabelAction = new QAction(tr("Copy label"), this);
   QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
   QAction *copyEligibleAmountAction = new QAction(tr("Copy eligible amount"), this);

   contextMenu = new QMenu(this);
   contextMenu->addAction(copyLabelAction);
   contextMenu->addAction(copyAddressAction);
   contextMenu->addAction(copyAmountAction);
   contextMenu->addAction(copyEligibleAmountAction);

   // Connect actions
   connect(smartRewardsTable, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
   connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
   connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
   connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
   connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyEligibleAmount()));

   timer = new QTimer(this);
   connect(timer,SIGNAL(timeout()), this ,SLOT(timerHandler()));
   timer->start(1000);

  // updateRewardsList();
}
 
SmartrewardsList::~SmartrewardsList()
{
    delete ui;
}
 
 void SmartrewardsList::setModel(WalletModel *model)
{
    this->model = model;
    if(!model) {
         return;
    }
}

 void SmartrewardsList::updateRewardsList()
 {

     ui->tableWidget->setRowCount(0);
     int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

     std::map<QString, std::vector<COutput> > mapCoins;
     model->listCoins(mapCoins);

     int nNewRow = 0;

     //Smartrewards snapshot date
     QDateTime lastSmartrewardsSnapshotDateTimeUtc = QDateTime::currentDateTimeUtc();
     int currentDay = lastSmartrewardsSnapshotDateTimeUtc.toString("dd").toInt();
     if(currentDay < SMARTREWARDS_DAY){
        lastSmartrewardsSnapshotDateTimeUtc = lastSmartrewardsSnapshotDateTimeUtc.addMonths(-1);
     }
     int snapshotMonth = lastSmartrewardsSnapshotDateTimeUtc.toString("MM").toInt();
     int snapshotYear = lastSmartrewardsSnapshotDateTimeUtc.toString("yyyy").toInt();
     lastSmartrewardsSnapshotDateTimeUtc = QDateTime(QDate(snapshotYear, snapshotMonth, SMARTREWARDS_DAY), QTime(SMARTREWARDS_UTC_HOUR, 0), Qt::UTC);


     BOOST_FOREACH(const PAIRTYPE(QString, std::vector<COutput>)& coins, mapCoins) {
         QString sWalletAddress = coins.first;
         QString sWalletLabel = model->getAddressTableModel()->labelForAddress(sWalletAddress);
         if (sWalletLabel.isEmpty())
             sWalletLabel = tr("(no label)");

         ui->tableWidget->insertRow(nNewRow);

         CAmount totalAmountSum = 0;
         CAmount eligibleSmartrewardsSum = 0;
         CAmount txAmount = 0;
         QString inputAddress = "";
         CTxDestination addressRetout;

         BOOST_FOREACH(const COutput& out, coins.second) {

             CBitcoinAddress SMARTREWARDS_ADDRESS;
             SMARTREWARDS_ADDRESS = CBitcoinAddress("SU5bKb35xUV8aHG5dNarWHB3HBVjcCRjYo");

             CScript SMARTREWARDS_SCRIPT;
             SMARTREWARDS_SCRIPT = GetScriptForDestination(SMARTREWARDS_ADDRESS.Get());

             //extract tx input address
             CTxIn vin = out.tx->vin[0];
             CTransaction txPrev;
             uint256 hash;
             GetTransaction(vin.prevout.hash, txPrev, Params().GetConsensus(), hash, true);
             CTxDestination source;
             ExtractDestination(txPrev.vout[vin.prevout.n].scriptPubKey, source);
             CBitcoinAddress addressSource(source);

             //calc total tx amount
             totalAmountSum += out.tx->vout[out.i].nValue;
             txAmount = out.tx->vout[out.i].nValue;

             //get tx date
             int64_t nTimeTx = out.tx->GetTxTime();
             QDateTime txDateTime = QDateTime::fromTime_t((qint32)nTimeTx);
             QDateTime txDateTimeUtc = txDateTime.toUTC();


             if(txDateTimeUtc < lastSmartrewardsSnapshotDateTimeUtc){
                 //the tx occurred before the snapshot
                 eligibleSmartrewardsSum += txAmount;
             }
             else if(addressSource == SMARTREWARDS_ADDRESS){
                 //the tx occurred after the snapshot but is from a smartrwards address
                 eligibleSmartrewardsSum += txAmount;
             }

             ui->tableWidget->setItem(nNewRow, 0, new QTableWidgetItem(sWalletLabel));
             ui->tableWidget->setItem(nNewRow, 1, new QTableWidgetItem(sWalletAddress));
             ui->tableWidget->setItem(nNewRow, 2, new QTableWidgetItem(BitcoinUnits::format(nDisplayUnit, totalAmountSum)));

             //check min eligible amount to rewards
             if(totalAmountSum >= SMARTREWARDS_MINIMUM_AMOUNT * COIN){
                  ui->tableWidget->setItem(nNewRow, 3, new QTableWidgetItem(BitcoinUnits::format(nDisplayUnit, eligibleSmartrewardsSum)));
             }else{
                  ui->tableWidget->setItem(nNewRow, 3, new QTableWidgetItem(BitcoinUnits::format(nDisplayUnit, 0)));
             }

         }
         nNewRow++;

     }

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

void SmartrewardsList::on_updatePushButton_clicked()
{
    updateRewardsList();
}

void SmartrewardsList::timerHandler()
{
    //Smartrewards snapshot date
    QDateTime lastSmartrewardsSnapshotDateTimeUtc = QDateTime::currentDateTimeUtc();
    int currentDay = lastSmartrewardsSnapshotDateTimeUtc.toString("dd").toInt();
    if(currentDay < SMARTREWARDS_DAY){
       lastSmartrewardsSnapshotDateTimeUtc = lastSmartrewardsSnapshotDateTimeUtc.addMonths(-1);
    }
    int snapshotMonth = lastSmartrewardsSnapshotDateTimeUtc.toString("MM").toInt();
    int snapshotYear = lastSmartrewardsSnapshotDateTimeUtc.toString("yyyy").toInt();
    lastSmartrewardsSnapshotDateTimeUtc = QDateTime(QDate(snapshotYear, snapshotMonth, SMARTREWARDS_DAY), QTime(SMARTREWARDS_UTC_HOUR, 0), Qt::UTC);
    ui->countdownlabel->setText(lastSmartrewardsSnapshotDateTimeUtc.toString("yyyy-MM-dd hh:mm UTC"));
}
