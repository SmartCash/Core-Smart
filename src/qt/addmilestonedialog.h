// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_QT_ADDMILESTONEDIALOG_H
#define SMARTCASH_QT_ADDMILESTONEDIALOG_H

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTableWidgetItem>

#include "primitives/transaction.h"

namespace Ui {
    class AddMilestoneDialog;
}

class AddMilestoneDialog : public QDialog
{
    Q_OBJECT

    Ui::AddMilestoneDialog *ui;
    uint32_t nAmount;
    int64_t nTime;
    std::string strDescription;

public:
    explicit AddMilestoneDialog(QWidget *parent = 0);
    ~AddMilestoneDialog();

    int64_t GetDate();
    uint32_t GetAmount();
    std::string GetDescription();

private Q_SLOTS:
    void cancel();
    void finalize();
};

#endif // SMARTCASH_QT_ADDMILESTONEDIALOG_H
