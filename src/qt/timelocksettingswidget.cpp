// Copyright (c) 2020 The SmartCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validation.h"
#include "chainparams.h"
#include "consensus/consensus.h"

#include "timelocksettingswidget.h"

#define ONE_MONTH               (30.5 * 24 * 60 * 60)
#define ONE_YEAR                (31556952)

TimeLockSettingsWidget::TimeLockSettingsWidget(QWidget *parent) :
    QWidget(parent),
    nLockTime(0),
    bShowTermRewards(false)
{
    const int nAvgBlockTime = Params().GetConsensus().nPowTargetSpacing;

    QSizePolicy sizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);

    timeLockCustomBlocks = new QSpinBox();
    timeLockCustomBlocks->setVisible(false);
    timeLockCustomBlocks->setRange(1, 1000000000);
    timeLockCustomBlocks->setValue(chainActive.Height());

    timeLockCustomDate = new QDateTimeEdit();
    timeLockCustomDate->setVisible(false);
    timeLockCustomDate->setMinimumDateTime(QDateTime::currentDateTime());
    timeLockCustomDate->setCalendarPopup(true);
    timeLockCustomDate->setDisplayFormat("MMMM d yy hh:mm:ss");

    timeLockCombo = new QComboBox();
    timeLockCombo->setSizePolicy(sizePolicy);
    timeLockCombo->setToolTip("Lock a transaction to be spent at future time.");
    updateTimeLockCombo();

    connect(timeLockCustomBlocks, SIGNAL(valueChanged(int)), this, SLOT(timeLockCustomBlocksChanged(int)));
    connect(timeLockCustomDate, SIGNAL(dateTimeChanged(const QDateTime&)), this,
        SLOT(timeLockCustomDateChanged(const QDateTime&)));
    connect(timeLockCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(timeLockComboChanged(int)));

    layout = new QHBoxLayout(this);
    layout->addWidget(timeLockCombo);
    layout->addWidget(timeLockCustomBlocks);
    layout->addWidget(timeLockCustomDate);
}

void TimeLockSettingsWidget::setShowTermRewards(bool show)
{
//    bShowTermRewards = false;

    // Only enable if we passed first 1.3.4 block height
//    if (show && ((MainNet() && chainActive.Height() >= HF_V2_0_HEIGHT) ||
//                 (TestNet() && chainActive.Height() >= TESTNET_V2_0_HEIGHT))) {
//        bShowTermRewards = true;
//    }

    updateTimeLockCombo();
    reset();
    layout->update();
}

void TimeLockSettingsWidget::updateTimeLockCombo()
{
/*    if ( (MainNet() && chainActive.Height() < HF_V2_1_0_HEIGHT) ||
                 (TestNet() && chainActive.Height() < TESTNET_V2_1_0_HEIGHT) )
    {
    timeLockItems.clear();
    timeLockItems.emplace_back("LockTime or TermRewards", 0, false);
    timeLockItems.emplace_back("1 month", (int64_t)ONE_MONTH, true);
    timeLockItems.emplace_back("2 months", (int64_t)(2 * ONE_MONTH), true);
    timeLockItems.emplace_back("3 months", (int64_t)(3 * ONE_MONTH), true);
    timeLockItems.emplace_back("6 months", (int64_t)(6 * ONE_MONTH), true);
    timeLockItems.emplace_back("1 year TermRewards & 101K+", (int64_t)(1 * ONE_YEAR), true);
    timeLockItems.emplace_back("2 year TermRewards & 101K+", (int64_t)(2 * ONE_YEAR), true);
    timeLockItems.emplace_back("3 year TermRewards & 101K+", (int64_t)(3 * ONE_YEAR), true);
    timeLockItems.emplace_back("15 year SmartRetire & 101K+", (int64_t)(15 * ONE_YEAR), true);
    timeLockItems.emplace_back("1 year Disabled TermRewards", 573381, true);
    timeLockItems.emplace_back("2 year Disabled TermRewards", 1146763, true);
    timeLockItems.emplace_back("Custom (until block)", -1, false);
    timeLockItems.emplace_back("Custom (until date)", -1, true);
    }else {
*/    timeLockItems.clear();
    timeLockItems.emplace_back("LockTime", 0, false);
    timeLockItems.emplace_back("1 month", (int64_t)ONE_MONTH, true);
    timeLockItems.emplace_back("2 months", (int64_t)(2 * ONE_MONTH), true);
    timeLockItems.emplace_back("3 months", (int64_t)(3 * ONE_MONTH), true);
    timeLockItems.emplace_back("6 months", (int64_t)(6 * ONE_MONTH), true);
    timeLockItems.emplace_back("1 year", 573381, true);
    timeLockItems.emplace_back("2 year", 1146763, true);
    timeLockItems.emplace_back("Custom (until block)", -1, false);
    timeLockItems.emplace_back("Custom (until date)", -1, true);
//    }

    timeLockCombo->clear();
    for (const auto &i : timeLockItems) {
        timeLockCombo->addItem(std::get<0>(i));
    }
}
void TimeLockSettingsWidget::timeLockComboChanged(int index)
{
    if ((index < 0) || (index >= timeLockItems.size())) {
        return;
    }

    if (std::get<0>(timeLockItems[index]) == "Custom (until block)") {
        timeLockCustomDate->setVisible(false);
        timeLockCustomBlocks->setVisible(true);
        nLockTime = timeLockCustomBlocks->value();
    }
    else if (std::get<0>(timeLockItems[index]) == "Custom (until date)")
    {
        timeLockCustomDate->setVisible(true);
        timeLockCustomBlocks->setVisible(false);
        nLockTime = timeLockCustomDate->dateTime().toMSecsSinceEpoch() / 1000;
    }
    else
    {
        timeLockCustomDate->setVisible(false);
        timeLockCustomBlocks->setVisible(false);

        int64_t lockTime = std::get<1>(timeLockItems[index]);
        if (lockTime > 0) {
            if (std::get<2>(timeLockItems[index])) {
                nLockTime = lockTime + (QDateTime::currentMSecsSinceEpoch() / 1000);
            } else {
                nLockTime = lockTime + chainActive.Height();
            }
        } else {
            nLockTime = 0;
        }
    }
}

void TimeLockSettingsWidget::timeLockCustomBlocksChanged(int i)
{
    nLockTime = i;
}

void TimeLockSettingsWidget::timeLockCustomDateChanged(const QDateTime& dt)
{
    nLockTime = dt.toMSecsSinceEpoch() / 1000;
}

void TimeLockSettingsWidget::reset()
{
    timeLockCombo->setCurrentIndex(0);
    timeLockCustomBlocks->setVisible(false);
    timeLockCustomDate->setVisible(false);
}
