// Copyright (c) 2017-2019 The SmartCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_SMARTVOTING_H
#define BITCOIN_QT_SMARTVOTING_H

#include "primitives/transaction.h"
#include "platformstyle.h"
#include "sync.h"
#include "smartvotingmanager.h"
#include "smartproposal.h"
#include "util.h"

#include <QDialog>
#include <QMenu>
#include <QTimer>
#include <QWidget>
#include <QTableWidgetItem>

namespace Ui {
    class SmartVotingPage;
}

class WalletModel;
class ClientModel;
class OptionsModel;
class PlatformStyle;
class QModelIndex;

QT_BEGIN_NAMESPACE
class QItemSelection;
class QMenu;
class QModelIndex;
class QSortFilterProxyModel;
class QTableView;
QT_END_NAMESPACE

/** SmartrewardsList Manager page widget */
class SmartVotingPage : public QWidget
{
    Q_OBJECT

public:
    explicit SmartVotingPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~SmartVotingPage();

    void setWalletModel(WalletModel *model);

protected:
    void showEvent(QShowEvent* event) final;
    void hideEvent(QHideEvent* event) final;
private:
    Ui::SmartVotingPage *ui;
    const PlatformStyle * platformStyle;
    WalletModel *walletModel;

    QTimer lockTimer;
    SmartVotingManager *votingManager;
    std::vector<SmartProposalWidget*> vecProposalWidgets;
    std::map<SmartProposal, SmartHiveVoting::Type> mapVoteProposals;

public Q_SLOTS:
    void updateUI();
    void updateProposalUI();
    void proposalsUpdated(const std::string &strErr);
    void voteChanged();
    void selectAddresses();
    void castVotes();
    void updateRefreshLock();
    void refreshProposals(bool fForce = false);
    void scrollChanged(int value);
    void balanceChanged(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                        const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);
};
#endif // BITCOIN_QT_SMARTVOTING_H
