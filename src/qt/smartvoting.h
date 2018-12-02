// Copyright (c) 2017-2018 The SmartCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_SMARTVOTING_H
#define BITCOIN_QT_SMARTVOTING_H

#include "primitives/transaction.h"
#include "platformstyle.h"
#include "sync.h"
#include "smartvotingmanager.h"
#include "smartproposal.h"
#include "smartvoting/proposal.h"
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
class SmartProposalTabWidget;

QT_BEGIN_NAMESPACE
class QItemSelection;
class QMenu;
class QModelIndex;
class QSortFilterProxyModel;
class QTableView;
QT_END_NAMESPACE


class VoteKeyWidgetItem : public QTableWidgetItem
{
public:
    VoteKeyWidgetItem() : QTableWidgetItem() {}
    VoteKeyWidgetItem(const QString &text, int type = Type) : QTableWidgetItem(text, type) {}
    bool operator<(const QTableWidgetItem &other) const;

    enum Columns{
        COLUMN_CHECKBOX,
        COLUMN_KEY,
        COLUMN_ADDRESS,
        COLUMN_POWER
    };
};

struct VoteKeyItems
{
    VoteKeyWidgetItem* checkbox;
    VoteKeyWidgetItem* address;
    VoteKeyWidgetItem* power;

    VoteKeyItems(VoteKeyWidgetItem* checkbox, VoteKeyWidgetItem* address, VoteKeyWidgetItem* power) :
        checkbox(checkbox), address(address), power(power){}
};


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
    QTimer voteKeyUpdateTimer;
    SmartVotingManager *votingManager;
    std::vector<SmartProposalWidget*> vecProposalWidgets;
    std::map<SmartProposal, SmartHiveVoting::Type> mapVoteProposals;

    std::map<CKeyID, VoteKeyItems> mapVisibleKeys;

    void connectProposalTab(SmartProposalTabWidget *tabWidget);
    void disconnectProposalTab(SmartProposalTabWidget *tabWidget);
    
    bool LoadProposalTabs();
    bool RemoveProposal(const CInternalProposal &proposal);
public Q_SLOTS:
    void updateUI();
    void showManagementUI();
    void showVotingUI();
    void updateProposalUI();
    void createProposal();
    void showVoteKeysUI();
    void updateVotingElements();
    void updateVoteKeyUI();
    void registerVoteKey();
    void voteKeyCellChanged(int, int);
    void selectAllVoteKeys();
    void importVoteKey();
    void proposalsUpdated(const std::string &strErr);
    void voteChanged();
    void castVotes();
    void updateRefreshLock();
    void refreshProposals(bool fForce = false);
    void scrollChanged(int value);
    void balanceChanged(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                        const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);
private Q_SLOTS:

    void tabTitleChanged(SmartProposalTabWidget* tab, std::string &newTitle);
    void removalRequested(SmartProposalTabWidget* tab);
};
#endif // BITCOIN_QT_SMARTVOTING_H
