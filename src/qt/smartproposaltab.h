#ifndef SMARTPROPOSALTAB_H
#define SMARTPROPOSALTAB_H

#include <QFrame>
#include <QButtonGroup>
#include <QLineEdit>

#include "smartvoting/proposal.h"
#include "uint256.h"

class WalletModel;

class QProposalInput : public QLineEdit
{

    Q_OBJECT

public:
    explicit QProposalInput(QWidget* parent = nullptr) : QLineEdit(parent) {}
    ~QProposalInput() {}

protected:
    void focusInEvent(QFocusEvent *e) final;
    void focusOutEvent(QFocusEvent *e) final;

Q_SIGNALS:
    void focusObtained(QProposalInput * inputField);
    void focusLost(QProposalInput * inputField);
};


namespace Ui {
class SmartProposalTabWidget;
}

class SmartProposalTabWidget : public QFrame
{
    Q_OBJECT

public:
    explicit SmartProposalTabWidget(const CInternalProposal& proposal, WalletModel *model, QWidget *parent = 0);
    ~SmartProposalTabWidget();

    const CInternalProposal GetProposal() { return proposal; }

    void updateMilestones();

private:
    Ui::SmartProposalTabWidget *ui;

    CInternalProposal proposal;

    WalletModel* walletModel;

    void updateUI();

private Q_SLOTS:

    bool save();

    void focusObtained(QProposalInput * inputField);
    void focusLost(QProposalInput * inputField);

    void removeButtonClicked();
    void showAddressDialog();

    void addMilestone();
    void removeMilestone();
    void milestoneSelectionChanged();

    void publish();
    void showDetails();

    void published();

Q_SIGNALS:

    void titleChanged(SmartProposalTabWidget* tab, std::string &strNewTitle);
    void removeButtonClicked(SmartProposalTabWidget* tab);

};

#endif // SMARTPROPOSALTAB_H
