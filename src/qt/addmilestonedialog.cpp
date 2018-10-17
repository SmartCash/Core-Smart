// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addmilestonedialog.h"
#include "ui_addmilestonedialog.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "bitcoingui.h"
#include "guiutil.h"
#include "util.h"
#include "validation.h"

#include <regex>

#include <boost/foreach.hpp>

#include <QMenu>
#include <QMessageBox>
#include <QCalendarWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QScrollBar>
#include <QDateTime>
#include <QApplication>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QFlags>
#include <QIcon>
#include <QSettings>
#include <QString>
#include <QRegularExpression>

AddMilestoneDialog::AddMilestoneDialog(QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint),
    ui(new Ui::AddMilestoneDialog),
    nAmount(0),
    nTime(0),
    strDescription(std::string())
{
    ui->setupUi(this);

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(finalize()));
    connect(ui->cancelButton, SIGNAL(clicked()), this, SLOT(cancel()));

    QDate minDate = QDate::currentDate();
    minDate = minDate.addDays(15);

    ui->calendarWidget->setMinimumDate(minDate);

    this->setWindowTitle("Add proposal milestone");
}

AddMilestoneDialog::~AddMilestoneDialog()
{
    delete ui;
}

int64_t AddMilestoneDialog::GetDate()
{
    return nTime;
}

uint32_t AddMilestoneDialog::GetAmount()
{
    return nAmount;
}

std::string AddMilestoneDialog::GetDescription()
{
    return strDescription;
}

void AddMilestoneDialog::finalize()
{

    if( !ParseUInt32(ui->amountField->text().toStdString(), &nAmount) ){
        showErrorDialog(this, "Amount needs to be a number.");
        return;
    }

    std::string strDesc = ui->descriptionField->text().toStdString();
    std::string strClean = strDesc;

    strClean.erase(std::remove_if(strClean.begin(), strClean.end(), char_isspace), strClean.end());

    if( !strClean.size() ){
        showErrorDialog(this, "You need to enter a description.");
        return;
    }

    strDescription = strDesc;

    QDate selectedDate = ui->calendarWidget->selectedDate();
    QDateTime milestoneDate(selectedDate);
    milestoneDate.setTimeSpec(Qt::UTC);
    nTime = milestoneDate.toTime_t();

    done(QDialog::Accepted);
}

void AddMilestoneDialog::cancel()
{
    done(QDialog::Rejected);
}
