#include "smartrewardentry.h"
#include "ui_smartrewardentry.h"
#include "smartrewards/rewards.h"
#include "amount.h"
#include "guiutil.h"
#include "guiconstants.h"
#include "bitcoinunits.h"
#include "math.h"

#include <QMenu>
#include <QContextMenuEvent>

QSmartRewardEntry::QSmartRewardEntry(const QString& strLabel, const QString& strAddress, CAmount nBalanceAtStart, QWidget *parent):
    QFrame(parent),
    ui(new Ui::QSmartRewardEntry),
    contextMenu(nullptr),
    nBalanceAtStart(nBalanceAtStart)
{
    ui->setupUi(this);

    ui->lblLabel->setText(strLabel);
    ui->lblAddress->setText(strAddress);

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
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyEligibleAmountAction, SIGNAL(triggered()), this, SLOT(copyEligibleAmount()));
    connect(copyRewardAction, SIGNAL(triggered()), this, SLOT(copyReward()));
}


QSmartRewardEntry::~QSmartRewardEntry()
{
    delete ui;
}

void QSmartRewardEntry::setDisqualifyingTx(const uint256& txHash){

    if( disqualifyingTx.IsNull() ){

        disqualifyingTx = txHash;

        QAction *copyHashAction = new QAction(tr("Copy disqualifying tx-hash"), this);
        contextMenu->addAction(copyHashAction);
        connect(copyHashAction, SIGNAL(triggered()), this, SLOT(copyDisqualifyingTxHash()));
    }
}

void QSmartRewardEntry::setBalance(CAmount nBalance)
{
    ui->lblBalance->setText(BitcoinUnits::formatWithUnit(BitcoinUnit::SMART, nBalance));
}

void QSmartRewardEntry::setInfoText(const QString &strText, const QColor &color)
{
    ui->stackedWidget->setCurrentIndex(1);
    ui->lblInfo->setText(strText);
    ui->lblInfo->setStyleSheet(QString("color: rgb(%1, %2, %3);").arg(color.red()).arg(color.green()).arg(color.blue()));
}

void QSmartRewardEntry::setEligible(CAmount nEligible, CAmount nEstimated)
{
    this->nEligible = nEligible;
    ui->stackedWidget->setCurrentIndex(0);
    ui->lblEligible->setText(BitcoinUnits::formatWithUnit(BitcoinUnit::SMART, nEligible));
    QString strEstimated = QString::fromStdString(strprintf("%d", nEstimated/COIN));
    AddThousandsSpaces(strEstimated);
    ui->lblEstimated->setText(strEstimated + " SMART");
}

void QSmartRewardEntry::setVoted(bool fState)
{
    fVoted = fState;
}

void QSmartRewardEntry::setIsSmartNode(bool fState)
{
    fIsSmartNode = fState;
}

void QSmartRewardEntry::setVoteProofConfirmations(int nConfirmations)
{
    nVoteProofConfirmations = nConfirmations;
}

QString QSmartRewardEntry::Address() const
{
    return ui->lblAddress->text();
}

QSmartRewardEntry::State QSmartRewardEntry::CurrentState()
{
    if( nBalanceAtStart < SMART_REWARDS_MIN_BALANCE ) return LowBalance;

    if( fIsSmartNode ) return IsASmartNode;

    if( !disqualifyingTx.IsNull() ) return OutgoingTransaction;

    if( !fVoted ) return VotingRequired;

    if( nVoteProofConfirmations == -1 ) return VoteProofRequired;

    if( nVoteProofConfirmations < Params().GetConsensus().nRewardsConfirmationsRequired ) return VoteProofConfirmationsRequired;

    if( nEligible ) return IsEligible;

    return Unknown;
}

string QSmartRewardEntry::ToString()
{
    return strprintf("QSmartRewardEntry( address: %s, currentState: %d, balanceAtStart: %d )\n", ui->lblAddress->text().toStdString(), CurrentState(), BalanceAtStart());
}

void QSmartRewardEntry::contextMenuEvent(QContextMenuEvent *event)
{
    contextMenu->exec(QCursor::pos());
}

void QSmartRewardEntry::copyLabel()
{
    GUIUtil::setClipboard(ui->lblLabel->text());
}

void QSmartRewardEntry::copyAddress()
{
    GUIUtil::setClipboard(ui->lblAddress->text());
}

void QSmartRewardEntry::copyAmount()
{
    GUIUtil::setClipboard(ui->lblBalance->text());
}

void QSmartRewardEntry::copyEligibleAmount()
{
    GUIUtil::setClipboard(ui->lblEligible->text());
}

void QSmartRewardEntry::copyReward()
{
    GUIUtil::setClipboard(ui->lblEstimated->text());
}

void QSmartRewardEntry::copyDisqualifyingTxHash()
{
    GUIUtil::setClipboard(QString::fromStdString(disqualifyingTx.ToString()));
}


