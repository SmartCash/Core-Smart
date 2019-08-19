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
    explicit QSmartRewardEntry(const QString& strLabel, const QString& strAddress, QWidget *parent = 0);
    ~QSmartRewardEntry();

    void setDisqualifyingTx(const uint256& txHash);
    void setBalance(CAmount nBalance);
    void setInfoText(const QString& strText, const QColor& color);
    void setEligible(CAmount nEligible, CAmount nEstimated);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    Ui::QSmartRewardEntry *ui;

    QMenu* contextMenu;

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
