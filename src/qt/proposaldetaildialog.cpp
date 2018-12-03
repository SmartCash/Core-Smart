#include "proposaldetaildialog.h"
#include "ui_proposaldetaildialog.h"
#include "guiutil.h"

#include <QDesktopServices>
#include <QUrl>

ProposalDetailDialog::ProposalDetailDialog(const CInternalProposal& proposal, QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint),
    ui(new Ui::ProposalDetailDialog),
    proposal(proposal)
{
    ui->setupUi(this);

    connect(ui->txExplorerButton, SIGNAL(clicked()), this, SLOT(openExplorer()));
    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(close()));

    connect(ui->hashCopyButton, SIGNAL(clicked()), this, SLOT(copyProposalHash()));
    connect(ui->sigCopyButton, SIGNAL(clicked()), this, SLOT(copySignature()));
    connect(ui->txCopyButton, SIGNAL(clicked()), this, SLOT(copyTransactionHash()));
    connect(ui->copyRawProposalButton, SIGNAL(clicked()), this, SLOT(copyRawProposal()));

    ui->hashLabel->setText(QString::fromStdString(proposal.GetHash().ToString()));
    ui->signatureLabel->setText(QString::fromStdString(proposal.GetSignedHash()));
    ui->txLabel->setText(QString::fromStdString(proposal.GetFeeHash().ToString()));

    this->setWindowTitle("Proposal details");
}

ProposalDetailDialog::~ProposalDetailDialog()
{
    delete ui;
}

void ProposalDetailDialog::openExplorer()
{
    QDesktopServices::openUrl(QUrl(QString::fromStdString("https://insight.smartcash.cc/tx/" + proposal.GetFeeHash().ToString())));
}

void ProposalDetailDialog::close()
{
    done(QDialog::Accepted);
}

void ProposalDetailDialog::copyProposalHash()
{
    GUIUtil::setClipboard(ui->hashLabel->text());
}

void ProposalDetailDialog::copySignature()
{
    GUIUtil::setClipboard(ui->signatureLabel->text());
}

void ProposalDetailDialog::copyTransactionHash()
{
    GUIUtil::setClipboard(ui->txLabel->text());
}

void ProposalDetailDialog::copyRawProposal()
{
    CDataStream ssProposal(SER_NETWORK, PROTOCOL_VERSION);
    ssProposal << static_cast<CProposal>(proposal);
    std::string strRawProposal = HexStr(ssProposal.begin(), ssProposal.end());
    GUIUtil::setClipboard(QString::fromStdString(strRawProposal));
}
