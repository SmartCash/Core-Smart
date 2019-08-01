// Copyright (c) 2017 - 2019 - The SmartCash Developers
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
    explicit CastVotesDialog(const PlatformStyle *platformStyle, SmartVotingManager *votingManager, WalletModel *model, QWidget *parent = 0);
    ~CastVotesDialog();

    void setVoting(std::map<SmartProposal, SmartHiveVoting::Type> mapVotings){this->mapVotings = mapVotings;}

private:
    Ui::CastVotesDialog *ui;

    const PlatformStyle *platformStyle;

    SmartVotingManager * votingManager;
    WalletModel *walletModel;
    std::map<SmartProposal, SmartHiveVoting::Type> mapVotings;
    std::vector<SmartProposalVote> vecVotes;
    QTimer waitTimer;

    void voteOne();
Q_SIGNALS:
    void votedForAddress(QString &address, int proposalId, bool successful);

public Q_SLOTS:
    int exec() final;

private Q_SLOTS:
    void start();
    void close();
    void waitForResponse();
    void voted(const SmartProposalVote &vote, const QJsonArray &result, const std::string &strErr);
};

#endif // SMARTCASH_QT_CASTVOTESDIALOG_H
