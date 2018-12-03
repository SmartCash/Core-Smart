#ifndef SMARTPROPOSAL_H
#define SMARTPROPOSAL_H

#include <QFrame>
#include <QButtonGroup>

#include "smartvoting/manager.h"
#include "smartvoting/proposal.h"

class WalletModel;

namespace Ui {
class SmartProposalWidget;
}

class SmartProposalWidget : public QFrame
{
    Q_OBJECT

public:
    explicit SmartProposalWidget(const CProposal *cProposal, WalletModel *walletModel, QWidget *parent = 0);
    ~SmartProposalWidget();

    vote_signal_enum_t GetVoteSignal();
    vote_outcome_enum_t GetVoteOutcome();
    bool votedValid();
    bool votedFunding();

    void UpdateFromProposal(const CProposal *proposal);
private:
    Ui::SmartProposalWidget *ui;
    WalletModel *walletModel;

    uint256 hash;
    QString title;
    QString url;
    double amountSmart;
    double amountUSD;
    QString votingValidDeadline;
    QString votingFundingDeadline;
    QString createdDate;
    int voteYesValid;
    int voteNoValid;
    int voteAbstainValid;
    double percentYesValid;
    double percentNoValid;
    double percentAbstainValid;
    int voteYesFunding;
    int voteNoFunding;
    int voteAbstainFunding;
    double percentYesFunding;
    double percentNoFunding;
    double percentAbstainFunding;

    std::map <CVoteKey, vote_outcome_enum_t> mapVotesValid;
    std::map <CVoteKey, vote_outcome_enum_t> mapVotesFunding;

    QButtonGroup signalSelection;
    QButtonGroup outcomeSelection;
    QButtonGroup resultSelection;

    void UpdateVotes(const CProposal *proposal);
    void UpdateUI();

    int GetVoteResultAmount(vote_signal_enum_t signal, vote_outcome_enum_t outcome);
    double GetVoteResultPercent(vote_signal_enum_t signal, vote_outcome_enum_t outcome);
    int GetVotedAmount(vote_signal_enum_t signal, vote_outcome_enum_t outcome);

private Q_SLOTS:
    void viewProposal();
    void viewPortal();
    void UpdateResult();
    void voteButtonClicked();
    void copyProposalHash();

Q_SIGNALS:
    void voteChanged();

};

#endif // SMARTPROPOSAL_H
