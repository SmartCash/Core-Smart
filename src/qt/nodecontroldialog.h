// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_QT_SMARTNODECONTROLDIALOG_H
#define SMARTCASH_QT_SMARTNODECONTROLDIALOG_H

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTableWidgetItem>

#include "primitives/transaction.h"

class PlatformStyle;
class WalletModel;

class CTxMemPool;
class COutPoint;

namespace Ui {
    class SmartnodeControlDialog;
}

enum SmartnodeControlMode
{
    Create,
    Edit,
    Remove,
    View
};

class CSmartnodeControlWidgetItem : public QTableWidgetItem
{
public:
    CSmartnodeControlWidgetItem(const QString &text, int type = Type) : QTableWidgetItem(text, type) {}
    bool operator<(const QTableWidgetItem &other) const;
};

class SmartnodeControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SmartnodeControlDialog(const PlatformStyle *platformStyle, SmartnodeControlMode mode, QWidget *parent = 0);
    ~SmartnodeControlDialog();

    void showError(QString message);
    void setSmartnodeData(int entryIndex, QString alias, QString ip, QString smartnodeKey, QString txHash, QString txIndex);
    void setModel(WalletModel *model);

    COutPoint unlockedForEdit;

    std::string GetAlias(){return alias;}
    std::string GetIpAddress(){return ip;}
    std::string GetSmartNodeKey(){return smartnodeKey;}
    std::string GetTxHash(){return txHash;}
    std::string GetTxIndex(){return txIndex;}
    bool validateSmartnodeIPAddress(QString ip);

    enum
    {
        COLUMN_LABEL = 0,
        COLUMN_ADDRESS,
        COLUMN_TXHASH,
        COLUMN_TXID
    };

private:
    Ui::SmartnodeControlDialog *ui;
    WalletModel *model;
    SmartnodeControlMode mode;

    int sortColumn;
    Qt::SortOrder sortOrder;

    const PlatformStyle *platformStyle;

    void updateView();
    void sortView(int, Qt::SortOrder);

    int entryIndex;
    std::string alias;
    std::string ip;
    std::string smartnodeKey;
    std::string txHash;
    std::string txIndex;

private Q_SLOTS:
    void copySmartnodeKey();
    void addCustomSmartnodeKey();
    void buttonBoxClicked(QAbstractButton*);
    void headerSectionClicked(int);
};

#endif // SMARTCASH_QT_SMARTNODECONTROLDIALOG_H
