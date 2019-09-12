#ifndef SMARTPROPOSAL_H
#define SMARTPROPOSAL_H

#include <QFrame>
#include <QButtonGroup>

#include "smartvotingmanager.h"

namespace Ui {
class SmartProposalWidget;
}

class SmartProposalWidget : public QFrame
{
    Q_OBJECT

public:
    explicit SmartProposalWidget(SmartProposal * proposal, QWidget *parent = 0);
    ~SmartProposalWidget();
    SmartProposal proposal;

    SmartHiveVoting::Type getVoteType();
    void setVoted(const SmartProposalVote &vote);
    bool voted();
private:
    Ui::SmartProposalWidget *ui;

    QButtonGroup voteSelection;
    SmartHiveVoting::Type voteState;

private Q_SLOTS:
    void viewProposal();
    void voteButtonClicked();

Q_SIGNALS:
    void voteChanged();

};

#endif // SMARTPROPOSAL_H
