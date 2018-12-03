// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_QT_CASTVOTESDIALOG_H
#define SMARTCASH_QT_CASTVOTESDIALOG_H

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTableWidgetItem>

#include "primitives/transaction.h"
#include "smartproposal.h"

class PlatformStyle;

namespace Ui {
    class CastVotesDialog;
}

class CastVotesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CastVotesDialog(const PlatformStyle *platformStyle,
                             WalletModel *model,
                             const std::map<uint256, std::pair<vote_signal_enum_t, vote_outcome_enum_t>> &mapVotings,
                             QWidget *parent = 0);
    ~CastVotesDialog();

private:
    Ui::CastVotesDialog *ui;
    const PlatformStyle *platformStyle;
    WalletModel *walletModel;
    std::map<uint256, std::pair<vote_signal_enum_t, vote_outcome_enum_t>> mapVotings;

    bool castVote(const CVoteKeySecret &voteKeySecret, const uint256 &hash, const vote_signal_enum_t eVoteSignal, const vote_outcome_enum_t eVoteOutcome, QString &strError);

public Q_SLOTS:
    int exec() final;

private Q_SLOTS:
    void start();
    void close();
};

#endif // SMARTCASH_QT_CASTVOTESDIALOG_H
