#ifndef SMARTREWARDENTRY_H
#define SMARTREWARDENTRY_H

#include <QFrame>
#include "amount.h"
#include "uint256.h"

namespace Ui {
class QSmartRewardEntry;
}

class QMenu;
class QContextMenuEvent;

class QSmartRewardEntry : public QFrame
{
    Q_OBJECT

public:
    explicit QSmartRewardEntry(const QString& strLabel, const QString& strAddress, CAmount nBalanceAtStart, QWidget *parent = 0);
    ~QSmartRewardEntry();

    enum State{
        Unknown,
        LowBalance,
        IsASmartNode,
        OutgoingTransaction,
        VotingRequired,
        VoteProofRequired,
        VoteProofConfirmationsRequired,
        IsEligible
    };

    void setDisqualifyingTx(const uint256& txHash);
    void setBalance(CAmount nBalance);
    void setInfoText(const QString& strText, const QColor& color);
    void setEligible(CAmount nEligible, CAmount nEstimated);
    void setIsSmartNode(bool fState);
    void setVoted(bool fState);
    void setVoteProofConfirmations(int nConfirmations);

    QString Address() const;
    CAmount Balance() const { return nBalance; }
    CAmount BalanceAtStart() const { return nBalanceAtStart; }
    CAmount Eligible() const { return nEligible; }
    bool IsSmartNode() const { return fIsSmartNode; }
    bool Voted() const { return fVoted; }
    int VoteProofConfirmations() const { return nVoteProofConfirmations; }
    State CurrentState();

    std::string ToString();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    Ui::QSmartRewardEntry *ui;

    QMenu* contextMenu;

    CAmount nBalanceAtStart;
    CAmount nBalance;
    CAmount nEligible;
    bool fIsSmartNode;
    bool fVoted;
    int nVoteProofConfirmations;
    uint256 disqualifyingTx;

private Q_SLOTS:

    void copyLabel();
    void copyAddress();
    void copyAmount();
    void copyEligibleAmount();
    void copyReward();
    void copyDisqualifyingTxHash();

};

#endif // SMARTREWARDENTRY_H
