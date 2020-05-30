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
#include "guiconstants.h"
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
#include "smartrewardentry.h"

#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include <QMenu>
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QDateTime>
#include <QSortFilterProxyModel>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QSpacerItem>

struct QSmartRewardField
{
    QString label;
    QString address;
    CAmount balance;
    CAmount balanceAtStart;
    CAmount eligible;
    CAmount reward;
    uint256 disqualifyingTx;
    bool fIsSmartNode;
    bool fActivated;
    uint8_t bonusLevel;

    QSmartRewardField() : label(QString()), address(QString()),
                          balance(0), eligible(0),reward(0),
                          disqualifyingTx(),
                          fIsSmartNode(false), fActivated(false),
                          bonusLevel(CSmartRewardEntry::NoBonus) {}
};

struct SortSmartRewardWidgets
{
    bool operator()(QSmartRewardEntry* w1,
                    QSmartRewardEntry* w2) const
    {

        if( w1->CurrentState() > w2->CurrentState() ){
            return true;
        }

        if( w1->CurrentState() == w2->CurrentState() &&
                w1->BalanceAtStart() > w2->BalanceAtStart() ){
            return true;
        }

        return false;
    }
};


SmartrewardsList::SmartrewardsList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SmartrewardsList),
    model(nullptr),
    clientModel(nullptr),
    platformStyle(platformStyle),
    state(STATE_INIT)
{
    ui->setupUi(this);

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

    connect(ui->scrollArea->verticalScrollBar(), SIGNAL(valueChanged(int)),this,SLOT(scrollChanged(int)));


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

void SmartrewardsList::updateOverviewUI(const CSmartRewardRound &currentRound, const CBlockIndex *tip)
{

    if( !currentRound.Is_1_3() ){
        ui->btnSendProofs->hide();
    }else{
        ui->btnSendProofs->show();
    }

    QString percentText;
    percentText.sprintf("%.2f%%", currentRound.percent * 100 * 52);
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
                roundEndText += QString("%1 day%2").arg(days).arg(days > 1 ? "s":"");
            }

            if( hours ){
                if( days ) roundEndText += ", ";
                roundEndText += QString("%1 hour%2").arg(hours).arg(hours > 1 ? "s":"");
            }

            if( !days && minutes ){
                if( hours ) roundEndText += ", ";
                roundEndText += QString("%1 minute%2").arg(minutes).arg(minutes > 1 ? "s":"");
            }

            roundEndText += " )";
        }

    }else{

        int64_t currentTime = QDateTime::currentMSecsSinceEpoch() / 1000;

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

    int nAvailableForProof = 0;
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
                    CSmartRewardEntry *reward = nullptr;

                    change.address = sAddress;
                    change.label = tr("(change)");
                    change.balance = out.tx->vout[out.i].nValue;

                    if( prewards->GetRewardEntry(CSmartAddress::Legacy(sAddress.toStdString()), reward, false) ){
                        change.balance = reward->balance;
                        change.fIsSmartNode = !reward->smartnodePaymentTx.IsNull();
                        change.balanceAtStart = reward->balanceAtStart;
                        change.disqualifyingTx = reward->disqualifyingTx;
                        change.fActivated = reward->fActivated;

                        if( !currentRound.Is_1_3() ){
                            change.eligible = reward->balanceEligible && reward->disqualifyingTx.IsNull() ? reward->balanceEligible : 0;
                        }else{
                            change.eligible = reward->IsEligible() ? reward->balanceEligible : 0;
                        }

                        change.reward = currentRound.percent * change.eligible;

                        if( currentRound.Is_1_3() && !change.fActivated ){
                            ++nAvailableForProof;
                        }
                    }

                    if( change.balance || change.eligible ) rewardList.push_back(change);

                    continue;

                }else{

                    rewardField.label = model->getAddressTableModel()->labelForAddress(rewardField.address);

                    if (rewardField.label.isEmpty())
                        rewardField.label = tr("(no label)");
                }

            }

        }

        if( !rewardField.address.isEmpty() ){

            CSmartRewardEntry *reward = nullptr;

            if( prewards->GetRewardEntry(CSmartAddress::Legacy(rewardField.address.toStdString()), reward, false) ){
                rewardField.balance = reward->balance;
                rewardField.fIsSmartNode = !reward->smartnodePaymentTx.IsNull();
                rewardField.balanceAtStart = reward->balanceAtStart;
                rewardField.disqualifyingTx = reward->disqualifyingTx;
                rewardField.fActivated = reward->fActivated;
                rewardField.bonusLevel = reward->bonusLevel;

                if( !currentRound.Is_1_3() ){
                    rewardField.eligible = reward->balanceEligible && reward->disqualifyingTx.IsNull() ? reward->balanceEligible : 0;
                }else{
                    rewardField.eligible = reward->IsEligible() ? reward->balanceEligible : 0;
                }

                rewardField.reward = currentRound.percent * rewardField.eligible;

                if( currentRound.Is_1_3() && !rewardField.fActivated ){
                    ++nAvailableForProof;
                }
            }

            if( rewardField.balance || rewardField.eligible ) rewardList.push_back(rewardField);
        }
    }

    int nEligibleAddresses = 0;
    CAmount rewardSum = 0;

    auto entry = vecEntries.begin();

    while( entry != vecEntries.end() ){

        auto it = std::find_if(rewardList.begin(),
                               rewardList.end(),
                               [entry](QSmartRewardField& field) -> bool {
            return (*entry)->Address() == field.address;
        });

        if( it == rewardList.end() ) {
            delete *entry;
            entry = vecEntries.erase(entry);
        }else{
            ++entry;
        }
    }

    BOOST_FOREACH(const QSmartRewardField& field, rewardList) {

        QSmartRewardEntry* entry;

        auto it = std::find_if(vecEntries.begin(),
                               vecEntries.end(),
                               [field](QSmartRewardEntry *entry) -> bool {
            return entry->Address() == field.address;
        });

        if( it == vecEntries.end() ){
            entry = new QSmartRewardEntry(field.label, field.address, field.balanceAtStart, this);
            vecEntries.push_back(entry);
        }else{
            entry = *it;
        }

        entry->setBalance(field.balance);
        entry->setIsSmartNode(field.fIsSmartNode);
        entry->setActivated(field.fActivated);

        if( currentRound.Is_1_3() ){

            entry->setMinBalance(SMART_REWARDS_MIN_BALANCE_1_3);
            entry->setBonusText(field.bonusLevel);

            if( field.fIsSmartNode ){
                entry->setInfoText("Address belongs to a SmartNode.", COLOR_NEGATIVE);
            }else if( field.balanceAtStart < SMART_REWARDS_MIN_BALANCE_1_3 ){
                entry->setInfoText(QString("Qualified balance is only %1 SMART at the round's startblock. Minimum required: %2 SMART. It can be activated now but it will not receive rewards until it has enough funds.").arg(BitcoinUnits::format(BitcoinUnit::SMART, field.balanceAtStart)).arg(SMART_REWARDS_MIN_BALANCE_1_3/COIN), COLOR_NEGATIVE);
            }else if( !field.disqualifyingTx.IsNull() ){
                entry->setDisqualifyingTx(field.disqualifyingTx);
                entry->setInfoText(QString("Address disqualified due to an outgoing transaction with the hash %1. It can be activated now but it will not receive any rewards until it becomes eligible").arg(QString::fromStdString(field.disqualifyingTx.ToString())), COLOR_NEGATIVE);
            }else if( field.fActivated && !field.eligible ){
                entry->setInfoText(QString("Address is activated but is not eligible until the next round."), COLOR_WARNING);
            }else if( field.fActivated ){
                entry->setEligible(field.eligible, field.reward);
                ++nEligibleAddresses;
            }
        }else{

            entry->setMinBalance(SMART_REWARDS_MIN_BALANCE_1_2);

            if( field.balanceAtStart < SMART_REWARDS_MIN_BALANCE_1_2 ){
                entry->setInfoText(QString("Address only held %1 SMART at the round's startblock. Minimum required: %2 SMART").arg(BitcoinUnits::format(BitcoinUnit::SMART, field.balanceAtStart)).arg(SMART_REWARDS_MIN_BALANCE_1_2/COIN), COLOR_NEGATIVE);
            }else if( !field.disqualifyingTx.IsNull() ){
                entry->setDisqualifyingTx(field.disqualifyingTx);
                entry->setInfoText(QString("Address disqualified due to an outgoing transaction with the hash %1").arg(QString::fromStdString(field.disqualifyingTx.ToString())), COLOR_NEGATIVE);
            }else{
                entry->setEligible(field.eligible, field.reward);
                ++nEligibleAddresses;
            }
        }

        rewardSum += field.reward;
    }

    for( QWidget* line : vecLines ){
        ui->smartRewardsList->layout()->removeWidget(line);
        delete line;
    }

    vecLines.clear();

    for( QSmartRewardEntry* entry : vecEntries ){
        ui->smartRewardsList->layout()->removeWidget(entry);
    }

    QLayoutItem * item;

    while( ( item = ui->smartRewardsList->layout()->takeAt(0) ) != 0){
        delete item->widget();
        delete item;
    }

    std::sort(vecEntries.begin(), vecEntries.end(), SortSmartRewardWidgets());

    for (const auto &entry : vecEntries) {
        ui->smartRewardsList->layout()->addWidget(entry);

        // Add a horizontal line unless it's the last entry
        if( entry != vecEntries.back() ){
            QHBoxLayout* hBox = new QHBoxLayout();
            QWidget* lineContainer = new QWidget();
            QFrame* line = new QFrame(lineContainer);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Plain);
            hBox->addWidget(line);
            hBox->setSpacing(0);
            hBox->setContentsMargins(0, 0, 0, 0);
            lineContainer->setLayout(hBox);
            ui->smartRewardsList->layout()->addWidget(lineContainer);
            vecLines.push_back(lineContainer);
        }
    }

    if( nAvailableForProof ){
//    if( !fActivated ){
        ui->btnSendProofs->setText( QString(tr("Send ActivateRewards [%1]")).arg(nAvailableForProof) );
        ui->btnSendProofs->setEnabled(true);
    }else{
        ui->btnSendProofs->setText( tr("No addresses need to ActivateRewards") );
        ui->btnSendProofs->setEnabled(false);
    }

    ui->lblActiveAddresses->setText(QString::number(vecEntries.size()));
    ui->lblEligibleAddresses->setText(QString::number(nEligibleAddresses));
    QString strEstimated = QString::fromStdString(strprintf("%d", (rewardSum + 50000000)/COIN));
    AddThousandsSpaces(strEstimated);
    ui->lblTotalRewards->setText(strEstimated + " SMART");
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
        LOCK(cs_rewardscache);
        currentRound = *prewards->GetCurrentRound();
        tip = chainActive.Tip();
    }

    switch(state){
    case STATE_INIT:

        if( prewards->IsSynced() && !fReindex ){

            ui->spinnerWidget->stop();

            setState(STATE_OVERVIEW);
        }

        break;
    case STATE_OVERVIEW:
        updateOverviewUI(currentRound, tip);
        break;
    default:
        break;
    }

    if( ui->stackedWidget->currentIndex() != state){
        ui->stackedWidget->setCurrentIndex(state);
    }

}

void SmartrewardsList::scrollChanged(int value)
{

    // Force redrawing every few scroll steps since the method used to
    // show multiple widgets in a scroll view is not ideal and causes
    // weird drawings from time to time...
    if (ui->scrollArea->verticalScrollBar()->maximum() == value ||
        ui->scrollArea->verticalScrollBar()->minimum() == value ||
        !( value % 30 )   )
        this->repaint();

}

void SmartrewardsList::setState(SmartrewardsList::SmartRewardsListState state)
{
    this->state = state;
    updateUI();
}

void SmartrewardsList::on_btnSendProofs_clicked()
{
    SpecialTransactionDialog dlg(ACTIVATION_TRANSACTIONS, platformStyle);
    dlg.setModel(model);
    dlg.exec();
    updateUI();
}
