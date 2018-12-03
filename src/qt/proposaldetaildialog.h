// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_QT_PROPOSALDETAILDIALOG_H
#define SMARTCASH_QT_PROPOSALDETAILDIALOG_H

#include <QAbstractButton>
#include <QTimer>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTableWidgetItem>

#include "primitives/transaction.h"
#include "smartvoting/proposal.h"

namespace Ui {
    class ProposalDetailDialog;
}

class ProposalDetailDialog : public QDialog
{
    Q_OBJECT

    Ui::ProposalDetailDialog *ui;

    CInternalProposal proposal;

public:
    explicit ProposalDetailDialog(const CInternalProposal& proposal, QWidget *parent = 0);
    ~ProposalDetailDialog();

private Q_SLOTS:
    void openExplorer();
    void close();

    void copyProposalHash();
    void copySignature();
    void copyTransactionHash();
    void copyRawProposal();
};

#endif // SMARTCASH_QT_PROPOSALDETAILDIALOG_H
