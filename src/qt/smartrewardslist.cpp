#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "smartrewardslist.h"
#include "ui_smartrewardslist.h"

#include "smartrewards/rewards.h"
#include "smartrewards/rewardspayments.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "txmempool.h"
#include "walletmodel.h"
#include "coincontrol.h"
#include "smartrewardslist.h"
#include "wallet/wallet.h"
#include "clientmodel.h"
#include "validation.h"
#include "specialtransactiondialog.h"

#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableWidget>
#include <QTableWidgetItem>
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

struct QSmartRewardVoteProofField
{
    QString label;
    QString address;
    CAmount eligible;
    bool fVoted;
    int nVoteProofConfirmations;

    QSmartRewardVoteProofField() : label(QString()), address(QString()),
                          eligible(0), fVoted(false), nVoteProofConfirmations(-1) {}
};

bool CSmartRewardWidgetItem::operator<(const QTableWidgetItem &other) const {
    int column = other.column();
    if (column == SmartrewardsList::COLUMN_AMOUNT || column == SmartrewardsList::COLUMN_ELIGIBLE || column == SmartrewardsList::COLUMN_REWARD)
        return data(Qt::UserRole).toLongLong() < other.data(Qt::UserRole).toLongLong();
    return QTableWidgetItem::operator<(other);
}

bool CSmartRewardVoteProofWidgetItem::operator<(const QTableWidgetItem &other) const
{
    int column = other.column();
    if (column == SmartrewardsList::PROOF_COLUMN_ELIGIBLE)
        return data(Qt::UserRole).toLongLong() < other.data(Qt::UserRole).toLongLong();
    return QTableWidgetItem::operator<(other);
}

SmartrewardsList::SmartrewardsList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SmartrewardsList),
    model(nullptr),
    clientModel(nullptr),
    platformStyle(platformStyle),
    state(STATE_INIT)
{
    ui->setupUi(this);

    QTableWidget *smartRewardsTable = ui->tableWidget;

    smartRewardsTable->setAlternatingRowColors(true);
    smartRewardsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    smartRewardsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    smartRewardsTable->setSortingEnabled(true);
    smartRewardsTable->setShowGrid(false);
    smartRewardsTable->verticalHeader()->hide();

    smartRewardsTable->horizontalHeader()->setSectionResizeMode(COLUMN_LABEL, QHeaderView::ResizeToContents);
    smartRewardsTable->horizontalHeader()->setSectionResizeMode(COLUMN_ADDRESS, QHeaderView::ResizeToContents);
    smartRewardsTable->horizontalHeader()->setSectionResizeMode(COLUMN_AMOUNT, QHeaderView::ResizeToContents);
    smartRewardsTable->horizontalHeader()->setSectionResizeMode(COLUMN_ELIGIBLE, QHeaderView::ResizeToContents);
    smartRewardsTable->horizontalHeader()->setSectionResizeMode(COLUMN_REWARD, QHeaderView::Stretch);

    QTableWidget *smartRewardsVoteProofTable = ui->proofTable;

    smartRewardsVoteProofTable->setAlternatingRowColors(true);
    smartRewardsVoteProofTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    smartRewardsVoteProofTable->setSelectionMode(QAbstractItemView::SingleSelection);
    smartRewardsVoteProofTable->setSortingEnabled(true);
    smartRewardsVoteProofTable->setShowGrid(false);
    smartRewardsVoteProofTable->verticalHeader()->hide();

    smartRewardsVoteProofTable->horizontalHeader()->setSectionResizeMode(PROOF_COLUMN_LABEL, QHeaderView::ResizeToContents);
    smartRewardsVoteProofTable->horizontalHeader()->setSectionResizeMode(PROOF_COLUMN_ADDRESS, QHeaderView::ResizeToContents);
    smartRewardsVoteProofTable->horizontalHeader()->setSectionResizeMode(PROOF_COLUMN_ELIGIBLE, QHeaderView::ResizeToContents);
    smartRewardsVoteProofTable->horizontalHeader()->setSectionResizeMode(PROOF_COLUMN_VOTED, QHeaderView::ResizeToContents);
    smartRewardsVoteProofTable->horizontalHeader()->setSectionResizeMode(PROOF_COLUMN_PROVEN, QHeaderView::Stretch);

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

void SmartrewardsList::updateOverviewUI(const CSmartRewardRound &currentRound, const CBlockIndex *tip)
{

    static int64_t lastUpdate = 0;
    int nFirst_1_3_Round = Params().GetConsensus().nRewardsFirst_1_3_Round;
    int64_t currentTime = QDateTime::currentMSecsSinceEpoch() / 1000;

    if( !lastUpdate || currentTime - lastUpdate  > 10 ){
        lastUpdate = currentTime;
    }else{
        return;
    }

    ui->spinnerWidget->stop();

    QString percentText;
    percentText.sprintf("%.2f%%", currentRound.percent * 100);
    ui->percentLabel->setText(percentText);

    ui->roundLabel->setText(QString::number(currentRound.number));

    QDateTime roundEnd;
    roundEnd.setTime_t(currentRound.endBlockTime);
    QString roundEndText;

    if( ( ( MainNet() && currentRound.number >= nRewardsFirstAutomatedRound ) || TestNet() ) && tip ){

        int64_t remainingBlocks = currentRound.endBlockHeight - tip->nHeight;

        roundEndText = QString("%1 blocks ( ").arg(remainingBlocks);

        if( remainingBlocks <= 1 ) {
            ui->roundEndsLabel->setText("");
            roundEndText = QString("Snapshot has occurred. Payouts will begin at block %1").arg(currentRound.endBlockHeight + Params().GetConsensus().nRewardsPayoutStartDelay);
        }else{

            ui->roundEndsLabel->setText("Round ends:");

            uint64_t remainingSeconds = remainingBlocks * Params().GetConsensus().nPowTargetSpacing;
            uint64_t minutesLeft = remainingSeconds / 60;
            uint64_t days = minutesLeft / 1440;
            uint64_t hours = (minutesLeft % 1440) / 60;
            uint64_t minutes = (minutesLeft % 1440) % 60;

            if( days ){
                roundEndText += QString("%1day%2").arg(days).arg(days > 1 ? "s":"");
            }

            if( hours ){
                if( days ) roundEndText += ", ";
                roundEndText += QString("%1hour%2").arg(hours).arg(hours > 1 ? "s":"");
            }

            if( !days && minutes ){
                if( hours ) roundEndText += ", ";
                roundEndText += QString("%1minute%2").arg(minutes).arg(minutes > 1 ? "s":"");
            }

            roundEndText += " )";
        }

    }else{

        roundEndText = roundEnd.toString(Qt::SystemLocaleShortDate);

        if( currentRound.endBlockTime < currentTime ) {
            roundEndText += " ( Now )";
        }else{
            uint64_t minutesLeft = ( (uint64_t)currentRound.endBlockTime - currentTime ) / 60;
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
                if( hours ) roundEndText += ", ";
                roundEndText += QString("%1minute%2").arg(minutes).arg(minutes > 1 ? "s":"");
            }

            roundEndText += " )";
        }
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

                        if( currentRound.number < nFirst_1_3_Round ){
                            change.eligible = reward.balanceEligible;
                        }else{
                            change.eligible = reward.IsEligible() ? reward.balanceEligible : 0;
                        }

                        change.reward = currentRound.percent * change.eligible;
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

                if( currentRound.number < nFirst_1_3_Round ){
                    rewardField.eligible = reward.balanceEligible;
                }else{
                    rewardField.eligible = reward.IsEligible() ? reward.balanceEligible : 0;
                }

                rewardField.reward = currentRound.percent * rewardField.eligible;
            }

            if( rewardField.balance ) rewardList.push_back(rewardField);
        }
    }

    int nRow = 0;

    CAmount rewardSum = 0;

    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);

    ui->tableWidget->setSortingEnabled(false);
    std::function<CSmartRewardWidgetItem * (QString)> createItem = [](QString title) {
        CSmartRewardWidgetItem * item = new CSmartRewardWidgetItem(title);
        item->setTextAlignment(Qt::AlignCenter);
        return item;
    };

    BOOST_FOREACH(const QSmartRewardField& field, rewardList) {

        ui->tableWidget->insertRow(nRow);

        CSmartRewardWidgetItem *balanceItem = new CSmartRewardWidgetItem(BitcoinUnits::format(nDisplayUnit, field.balance) + " " +  BitcoinUnits::name(nDisplayUnit));
        CSmartRewardWidgetItem *eligibleItem = new CSmartRewardWidgetItem(BitcoinUnits::format(nDisplayUnit, field.eligible) + " " +  BitcoinUnits::name(nDisplayUnit));
        CSmartRewardWidgetItem *rewardItem = new CSmartRewardWidgetItem(BitcoinUnits::format(nDisplayUnit, field.reward) + " " +  BitcoinUnits::name(nDisplayUnit));

        ui->tableWidget->setItem(nRow, COLUMN_LABEL, createItem(field.label));
        ui->tableWidget->setItem(nRow, COLUMN_ADDRESS, createItem(field.address));
        ui->tableWidget->setItem(nRow, COLUMN_AMOUNT, balanceItem);
        balanceItem->setData(Qt::UserRole, QVariant((qlonglong)field.balance));
        ui->tableWidget->setItem(nRow, COLUMN_ELIGIBLE, eligibleItem);
        eligibleItem->setData(Qt::UserRole, QVariant((qlonglong)field.eligible));
        ui->tableWidget->setItem(nRow, COLUMN_REWARD, rewardItem);
        rewardItem->setData(Qt::UserRole, QVariant((qlonglong)field.reward));

        nRow++;
        rewardSum += field.reward;
    }

    ui->sumLabel->setText(BitcoinUnits::format(nDisplayUnit, rewardSum) + " " +  BitcoinUnits::name(nDisplayUnit));

    ui->tableWidget->setSortingEnabled(true);
}

void SmartrewardsList::updateVoteProofUI(const CSmartRewardRound &currentRound, const CBlockIndex *tip)
{

    ui->lblProofsTitleRound->setText(QString("%1").arg(currentRound.number));

    int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    std::map<QString, std::vector<COutput> > mapCoins;
    model->listCoins(mapCoins);

    int nAvailableForProof = 0;
    std::vector<QSmartRewardVoteProofField> proofList;

    BOOST_FOREACH(const PAIRTYPE(QString, std::vector<COutput>)& coins, mapCoins) {

        QString sWalletAddress = coins.first;
        QString sWalletLabel = model->getAddressTableModel()->labelForAddress(sWalletAddress);

        if (sWalletLabel.isEmpty())
            sWalletLabel = tr("(no label)");

        QSmartRewardVoteProofField proofField;
        CKeyID keyId;

        proofField.address = sWalletAddress;
        proofField.label = sWalletLabel;

        BOOST_FOREACH(const COutput& out, coins.second) {

            CTxDestination outputAddress;
            QString sAddress;

            if(ExtractDestination(out.tx->vout[out.i].scriptPubKey, outputAddress)){

                sAddress = QString::fromStdString(CBitcoinAddress(outputAddress).ToString());

                if (!(sAddress == sWalletAddress)){ // change address

                    QSmartRewardVoteProofField change;
                    CSmartRewardEntry reward;

                    change.address = sAddress;
                    change.label = tr("(change)");

                    if( prewards->GetRewardEntry(CSmartAddress(sAddress.toStdString()),reward) ){
                        change.eligible = reward.balanceEligible;

                        if( reward.id.GetKeyID(keyId) ){

                            LOCK(pwalletMain->cs_wallet);

                            change.fVoted = pwalletMain->mapVoted[keyId].find(currentRound.number) != pwalletMain->mapVoted[keyId].end();

                            if( pwalletMain->mapVoteProofs[keyId].find(currentRound.number) != pwalletMain->mapVoteProofs[keyId].end() ){

                                uint256 proofHash = pwalletMain->mapVoteProofs[keyId][currentRound.number];
                                change.nVoteProofConfirmations = 0;
                            }

                            if( pwalletMain->mapVoted[keyId].find(currentRound.number) != pwalletMain->mapVoted[keyId].end() &&
                                pwalletMain->mapVoteProofs[keyId].find(currentRound.number) == pwalletMain->mapVoteProofs[keyId].end() ){
                                ++nAvailableForProof;
                            }

                            proofList.push_back(change);
                        }
                    }

                    continue;

                }else{

                    proofField.label = model->getAddressTableModel()->labelForAddress(proofField.address);

                    if (proofField.label.isEmpty())
                        proofField.label = tr("(no label)");
                }

            }

        }

        if( !proofField.address.isEmpty() ){

            CSmartRewardEntry reward;

            if( prewards->GetRewardEntry(CSmartAddress(proofField.address.toStdString()), reward) ){

                proofField.eligible = reward.balanceEligible;

                if( reward.id.GetKeyID(keyId) ){

                    LOCK(pwalletMain->cs_wallet);

                    proofField.fVoted = pwalletMain->mapVoted[keyId].find(currentRound.number) != pwalletMain->mapVoted[keyId].end();

                    if( pwalletMain->mapVoteProofs[keyId].find(currentRound.number) != pwalletMain->mapVoteProofs[keyId].end() ){

                        uint256 proofHash = pwalletMain->mapVoteProofs[keyId][currentRound.number];
                        proofField.nVoteProofConfirmations = 0;
                    if( pwalletMain->mapVoted[keyId].find(currentRound.number) != pwalletMain->mapVoted[keyId].end() &&
                        pwalletMain->mapVoteProofs[keyId].find(currentRound.number) == pwalletMain->mapVoteProofs[keyId].end() ){
                        ++nAvailableForProof;
                    }

                    proofList.push_back(proofField);
                }
            }

        }
    }

    int nRow = 0;

    ui->proofTable->clearContents();
    ui->proofTable->setRowCount(0);

    ui->proofTable->setSortingEnabled(false);
    std::function<CSmartRewardVoteProofWidgetItem * (QString)> createItem = [](QString title) {
        CSmartRewardVoteProofWidgetItem * item = new CSmartRewardVoteProofWidgetItem(title);
        item->setTextAlignment(Qt::AlignCenter);
        return item;
    };

    BOOST_FOREACH(const QSmartRewardVoteProofField& field, proofList) {

        ui->proofTable->insertRow(nRow);

        QString strConfirmations;
        int nConfirmationsRequired = Params().GetConsensus().nRewardsConfirmationsRequired - field.nVoteProofConfirmations;

        if( field.nVoteProofConfirmations == -1 ){
            strConfirmations = tr("No");
        }else if( nConfirmationsRequired > 0 ){
            QString("%1").arg(nConfirmationsRequired) + tr("confirmations required");
        }else{
            tr("Yes");
        }

        CSmartRewardWidgetItem *eligibleItem = new CSmartRewardWidgetItem(BitcoinUnits::format(nDisplayUnit, field.eligible) + " " +  BitcoinUnits::name(nDisplayUnit));
        CSmartRewardWidgetItem *votedItem = new CSmartRewardWidgetItem(field.fVoted ? tr("Yes") : tr("No"));
        CSmartRewardWidgetItem *provenItem = new CSmartRewardWidgetItem(strConfirmations);

        ui->proofTable->setItem(nRow, PROOF_COLUMN_LABEL, createItem(field.label));
        ui->proofTable->setItem(nRow, PROOF_COLUMN_ADDRESS, createItem(field.address));
        ui->proofTable->setItem(nRow, PROOF_COLUMN_ELIGIBLE, eligibleItem);
        eligibleItem->setData(Qt::UserRole, QVariant((qlonglong)field.eligible));
        ui->proofTable->setItem(nRow, PROOF_COLUMN_VOTED, votedItem);
        votedItem->setData(Qt::UserRole, QVariant((bool)field.fVoted));
        ui->proofTable->setItem(nRow, PROOF_COLUMN_PROVEN, provenItem);
        provenItem->setData(Qt::UserRole, QVariant((qlonglong)field.nVoteProofConfirmations));

        nRow++;
    }

    if( nAvailableForProof ){
        ui->btnSendProofs->setText( QString(tr("Send VoteProofs [%1]")).arg(nAvailableForProof) );
        ui->btnSendProofs->setEnabled(true);
    }else{
        ui->btnSendProofs->setText( tr("No address ready for a VoteProof") );
        ui->btnSendProofs->setEnabled(false);
    }

    ui->proofTable->setSortingEnabled(true);
}

void SmartrewardsList::updateUI()
{
    // If the wallet model hasn't been set yet we cant update the UI.
    if(!model) {
        return;
    }

    CSmartRewardRound currentRound;
    CBlockIndex* tip = nullptr;
    {
        LOCK(cs_rewardrounds);
        currentRound = prewards->GetCurrentRound();
        tip = chainActive.Tip();
    }

    switch(state){
    case STATE_INIT:

        setState(STATE_PROCESSING);

        break;
    case STATE_PROCESSING:

        if( prewards->IsSynced() ){
            setState(STATE_OVERVIEW);
        }else{
            double progress = prewards->GetProgress() * ui->loadingProgress->maximum();
            ui->loadingProgress->setValue(progress);
        }

        break;
    case STATE_OVERVIEW:
        updateOverviewUI(currentRound, tip);
        break;
    case STATE_VOTEPROOF:
        updateVoteProofUI(currentRound, tip);
        break;
    default:
        break;
    }

    if( ui->stackedWidget->currentIndex() != state){
        ui->stackedWidget->setCurrentIndex(state);
    }

}

void SmartrewardsList::on_btnManageProofs_clicked()
{
    setState(STATE_VOTEPROOF);
}

void SmartrewardsList::setState(SmartrewardsList::SmartRewardsListState state)
{
    this->state = state;
    updateUI();
}

void SmartrewardsList::on_btnCancelProofs_clicked()
{
    setState(STATE_OVERVIEW);
}

void SmartrewardsList::on_btnSendProofs_clicked()
{
    SpecialTransactionDialog dlg(VOTE_PROOF_TRANSACTIONS, platformStyle);
    dlg.setModel(model);
    dlg.exec();
}
