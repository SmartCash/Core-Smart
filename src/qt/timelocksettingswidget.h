// Copyright (c) 2020 The SmartCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TIMELOCKSETTINGSWIDGET_H
#define BITCOIN_QT_TIMELOCKSETTINGSWIDGET_H

#include <vector>
#include <utility>

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QDateTimeEdit>
#include <QHBoxLayout>

class TimeLockSettingsWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool showTermRewards READ showTermRewards WRITE setShowTermRewards)

public:
    explicit TimeLockSettingsWidget(QWidget *parent = 0);

    int64_t getLockTime() { return nLockTime; }
    void reset();
    void updateTimeLockCombo();
    bool showTermRewards() const { return bShowTermRewards; }
    void setShowTermRewards(bool show);

private:
    QHBoxLayout *layout;
    QComboBox *timeLockCombo;
    QSpinBox *timeLockCustomBlocks;
    QDateTimeEdit *timeLockCustomDate;
    std::vector<std::pair<QString, int>> timeLockItems;
    int64_t nLockTime;
    bool bShowTermRewards;

private Q_SLOTS:
    void timeLockComboChanged(int);
    void timeLockCustomBlocksChanged(int);
    void timeLockCustomDateChanged(const QDateTime&);
};

#endif // BITCOIN_QT_TIMELOCKSETTINGSWIDGET_H
