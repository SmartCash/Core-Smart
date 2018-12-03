// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_QT_PUBLISHPROPOSALDIALOG_H
#define SMARTCASH_QT_PUBLISHPROPOSALDIALOG_H

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
    class PublishProposalDialog;
}

class PublishProposalDialog : public QDialog
{
    Q_OBJECT

    Ui::PublishProposalDialog *ui;

    QTimer timer;
    CInternalProposal proposal;

public:
    explicit PublishProposalDialog(const CInternalProposal& proposal, QWidget *parent = 0);
    ~PublishProposalDialog();

private Q_SLOTS:
    void openExplorer();
    void close();
    void update();


Q_SIGNALS:
    void published();

};

#endif // SMARTCASH_QT_PUBLISHPROPOSALDIALOG_H
