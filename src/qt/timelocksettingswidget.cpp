// Copyright (c) 2020 The SmartCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QHBoxLayout>

#include "validation.h"
#include "chainparams.h"

#include "timelocksettingswidget.h"

#define ONE_MONTH                     (30.5 * 24 * 60 * 60)
#define ONE_YEAR                      (365 * 24 * 60 * 60)

TimeLockSettingsWidget::TimeLockSettingsWidget(QWidget *parent) :
    QWidget(parent),
    nLockTime(0)
{
    const int nAvgBlockTime = Params().GetConsensus().nPowTargetSpacing;
    timeLockItems.emplace_back("Set LockTime", 0);
    timeLockItems.emplace_back("1 month", (int)(ONE_MONTH / nAvgBlockTime));
    timeLockItems.emplace_back("2 months", (int)(2 * ONE_MONTH / nAvgBlockTime));
    timeLockItems.emplace_back("3 months", (int)(3 * ONE_MONTH / nAvgBlockTime));
    timeLockItems.emplace_back("6 months", (int)(6 * ONE_MONTH / nAvgBlockTime));
    timeLockItems.emplace_back("1 year", (int)(ONE_YEAR / nAvgBlockTime));
    timeLockItems.emplace_back("Custom (until block)", -1);
    timeLockItems.emplace_back("Custom (until date)", -1);

    timeLockCombo = new QComboBox();
    for (const auto &i : timeLockItems) {
        timeLockCombo->addItem(i.first);
    }

    QSizePolicy sizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);

    timeLockCombo->setSizePolicy(sizePolicy);
    timeLockCombo->setToolTip("Lock a transaction to be spent at future time.");

    // Make Timelock feature visible only if supermajority enforced BIP65
    if(!IsSuperMajority(4, chainActive.Tip(), Params().GetConsensus().nMajorityEnforceBlockUpgrade,
          Params().GetConsensus()))
    {
        timeLockCombo->setVisible(false);
    }

    timeLockCustomBlocks = new QSpinBox();
    timeLockCustomBlocks->setVisible(false);
    timeLockCustomBlocks->setRange(1, 1000000);
    timeLockCustomBlocks->setValue(chainActive.Height());

    timeLockCustomDate = new QDateTimeEdit();
    timeLockCustomDate->setVisible(false);
    timeLockCustomDate->setMinimumDateTime(QDateTime::currentDateTime());
    timeLockCustomDate->setCalendarPopup(true);
    timeLockCustomDate->setDisplayFormat("MMMM d yy hh:mm:ss");

    connect(timeLockCustomBlocks, SIGNAL(valueChanged(int)), this, SLOT(timeLockCustomBlocksChanged(int)));
    connect(timeLockCustomDate, SIGNAL(dateTimeChanged(const QDateTime&)), this,
        SLOT(timeLockCustomDateChanged(const QDateTime&)));
    connect(timeLockCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(timeLockComboChanged(int)));

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->addWidget(timeLockCombo);
    layout->addWidget(timeLockCustomBlocks);
    layout->addWidget(timeLockCustomDate);
}

void TimeLockSettingsWidget::timeLockComboChanged(int index)
{
    if (timeLockItems[index].first == "Custom (until block)") {
        timeLockCustomDate->setVisible(false);
        timeLockCustomBlocks->setVisible(true);
        nLockTime = timeLockCustomBlocks->value();
    }
    else if (timeLockItems[index].first == "Custom (until date)")
    {
        timeLockCustomDate->setVisible(true);
        timeLockCustomBlocks->setVisible(false);
        nLockTime = timeLockCustomDate->dateTime().toMSecsSinceEpoch() / 1000;
    }
    else
    {
        timeLockCustomDate->setVisible(false);
        timeLockCustomBlocks->setVisible(false);
        nLockTime = timeLockItems[index].second > 0 ? chainActive.Height() + timeLockItems[index].second : 0;
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
