
#include "ui_smartproposaltab.h"
#include "addmilestonedialog.h"
#include "amount.h"
#include "bitcoinunits.h"
#include "bitcoingui.h"
#include "core_io.h"
#include "math.h"
#include "smartproposaltab.h"
#include "proposaladdressdialog.h"
#include "publishproposaldialog.h"
#include "proposaldetaildialog.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "timedata.h"
#include "sendcoinsdialog.h"
#include "validation.h"

#include <QDateTime>

extern void EnsureWalletIsUnlocked();

SmartProposalTabWidget::SmartProposalTabWidget(const CInternalProposal &proposal, WalletModel *model, QWidget *parent) :
    QFrame(parent),
    ui(new Ui::SmartProposalTabWidget),
    proposal(proposal),
    walletModel(model)
{
    ui->setupUi(this);

    ui->removeMilestoneButton->setEnabled(false);

    connect(ui->titleField, SIGNAL(focusLost(QProposalInput*)),this,SLOT(focusLost(QProposalInput*)));
    connect(ui->titleField, SIGNAL(focusObtained(QProposalInput*)),this,SLOT(focusObtained(QProposalInput*)));

    connect(ui->urlField, SIGNAL(focusLost(QProposalInput*)),this,SLOT(focusLost(QProposalInput*)));
    connect(ui->urlField, SIGNAL(focusObtained(QProposalInput*)),this,SLOT(focusObtained(QProposalInput*)));

    connect(ui->selectAddressButton, SIGNAL(clicked()),this,SLOT(showAddressDialog()));
    connect(ui->removeButton, SIGNAL(clicked()),this,SLOT(removeButtonClicked()));

    connect(ui->addMilestoneButton, SIGNAL(clicked()),this,SLOT(addMilestone()));
    connect(ui->removeMilestoneButton, SIGNAL(clicked()),this,SLOT(removeMilestone()));
    connect(ui->milestoneTable, SIGNAL(itemSelectionChanged()), this, SLOT(milestoneSelectionChanged()));

    connect(ui->publishButton, SIGNAL(clicked()),this,SLOT(publish()));
    connect(ui->detailsButton, SIGNAL(clicked()),this,SLOT(showDetails()));

    ui->milestoneTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->milestoneTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->milestoneTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    updateUI();
}

SmartProposalTabWidget::~SmartProposalTabWidget()
{
    delete ui;
}

void SmartProposalTabWidget::updateUI()
{

    bool fPaid = proposal.IsPaid();
    bool fPublished = proposal.IsPublished();

    ui->publishButton->show();

    if( fPaid || fPublished ){

        ui->detailsButton->show();

        if( fPaid ){

            ui->removeButton->hide();

            ui->titleField->setEnabled(false);
            ui->urlField->setEnabled(false);

            ui->addMilestoneButton->hide();
            ui->removeMilestoneButton->hide();

            ui->selectAddressButton->hide();

        }

        if( fPublished ){

            ui->publishButton->hide();

        }

    }else{

        ui->removeButton->show();

        ui->titleField->setEnabled(true);
        ui->urlField->setEnabled(true);

        ui->addMilestoneButton->show();
        ui->removeMilestoneButton->show();

        ui->selectAddressButton->show();

        ui->detailsButton->hide();
    }

    ui->titleField->setText(QString::fromStdString(proposal.GetTitle()));
    ui->urlField->setText(QString::fromStdString(proposal.GetUrl()));

    CSmartAddress address = proposal.GetAddress();
    std::string strAddress = "No address selected";

    if( address.IsValid() )
        strAddress = address.ToString();

    ui->addressLabel->setText(QString::fromStdString(strAddress));

    updateMilestones();
}

bool SmartProposalTabWidget::save()
{

    if( !pwalletMain ){
        showErrorDialog(this, "Wallet not available");
        return false;
    }

    LOCK(pwalletMain->cs_wallet);

    CWalletDB walletdb(pwalletMain->strWalletFile);

    std::map<uint256, CInternalProposal> mapProposals;

    mapProposals.clear();

    walletdb.ReadProposals(mapProposals);

    if( !mapProposals.count(proposal.GetInternalHash()))
        mapProposals.insert(std::make_pair(proposal.GetInternalHash(), proposal));
    else
        mapProposals[proposal.GetInternalHash()] = proposal;

    if( !walletdb.WriteProposals(mapProposals) ){
        showErrorDialog(this, "Failed to save the proposal.");
        return false;
    }

    return true;
}
void SmartProposalTabWidget::focusObtained(QProposalInput *inputField)
{
   //Select the text of the field
   QTimer::singleShot(0,inputField,SLOT(selectAll()));
}

void SmartProposalTabWidget::focusLost(QProposalInput *inputField)
{
    // Clear text selection
    inputField->deselect();

    std::string strError;
    std::string strNewText = inputField->text().toStdString();

    if( inputField == ui->titleField ){

        proposal.SetTitle(strNewText);

        if( save() ){
            Q_EMIT titleChanged(this, strNewText);
            return;
        }
    }

    if( inputField == ui->urlField ){

        proposal.SetUrl(strNewText);
        save();
    }

}

void SmartProposalTabWidget::removeButtonClicked()
{
    Q_EMIT removeButtonClicked(this);
}

void SmartProposalTabWidget::showAddressDialog()
{
    CSmartAddress address;
    ProposalAddressDialog dlg;

    dlg.exec();

    QString strAddress = dlg.GetAddress();

    if( strAddress == QString() ){
        ui->addressLabel->setText("No address selected");
    }else{
        address = CSmartAddress(strAddress.toStdString());
        ui->addressLabel->setText(strAddress);
        save();
    }

    proposal.SetAddress(address);
}

void QProposalInput::focusInEvent(QFocusEvent *e)
{
    Q_EMIT focusObtained(this);
}

void QProposalInput::focusOutEvent(QFocusEvent *e)
{
    Q_EMIT focusLost(this);
}

void SmartProposalTabWidget::updateMilestones()
{

    int nRow = 0;

    uint32_t amountSum = 0;

    ui->milestoneTable->clearContents();
    ui->milestoneTable->setRowCount(0);

    ui->milestoneTable->setSortingEnabled(false);

    for(const CProposalMilestone& milestone : proposal.GetMilestones() ) {

        ui->milestoneTable->insertRow(nRow);

        QString amountString = QString::number(milestone.GetAmount());
        AddThousandsSpaces(amountString);

        QDateTime milestoneDate;
        milestoneDate.setTimeSpec(Qt::UTC);
        milestoneDate.setTime_t(milestone.GetTime());

        QTableWidgetItem *dateItem = new QTableWidgetItem(milestoneDate.toString("d. MMMM yyyy"));
        QTableWidgetItem *amountItem = new QTableWidgetItem(amountString + " USD");
        QTableWidgetItem *descriptionItem = new QTableWidgetItem(QString::fromStdString(milestone.GetDescription()));

        ui->milestoneTable->setItem(nRow, 0, dateItem);
        ui->milestoneTable->setItem(nRow, 1, amountItem);
        ui->milestoneTable->setItem(nRow, 2, descriptionItem);

        nRow++;
        amountSum += milestone.GetAmount();
    }

    QString finalAmountString = QString::number(amountSum);
    AddThousandsSpaces(finalAmountString);
    ui->finalAmountLabel->setText(finalAmountString + " USD");
}

void SmartProposalTabWidget::addMilestone()
{
    AddMilestoneDialog dlg;

    if(dlg.exec()){

        std::string strError;
        CProposalMilestone milestone(dlg.GetDate(), dlg.GetAmount(), dlg.GetDescription());

        CInternalProposal validate = proposal;

        validate.AddMilestone(milestone);

        if( !validate.IsMilestoneVectorValid(strError) ){
            showErrorDialog(this, QString::fromStdString(strError));
            return;
        }

        proposal.AddMilestone(milestone);

        if( save() )
            updateMilestones();

    }
}

void SmartProposalTabWidget::removeMilestone()
{
    QItemSelectionModel *select = ui->milestoneTable->selectionModel();

    if( select->hasSelection() ){
        proposal.RemoveMilestone(select->selectedRows()[0].row());

        if( save() )
            updateMilestones();
    }

}

void SmartProposalTabWidget::milestoneSelectionChanged()
{
    QItemSelectionModel *select = ui->milestoneTable->selectionModel();
    ui->removeMilestoneButton->setEnabled(select->hasSelection());
}

void SmartProposalTabWidget::publish()
{

    if( !proposal.IsPaid() ){

        if( !pwalletMain ){
            showErrorDialog(this, "Wallet not available.");
            return;
        }

        int64_t nTime = GetAdjustedTime();
        std::vector<std::string> vecErrors;

        proposal.SetCreationTime(nTime);

        if( !proposal.IsValid(vecErrors) ){

            std::string strError;

            for( auto error : vecErrors ){
                strError += error + "\n";
            }

            showErrorDialog(this, QString::fromStdString("Invalid proposal data, error messages:\n" + strError));
            return;
        }

        LOCK2(cs_main, pwalletMain->cs_wallet);

        WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();
        bool fLocked = encStatus == walletModel->Locked;

        std::unique_ptr<WalletModel::UnlockContext> ctx = fLocked ?
                    std::unique_ptr<WalletModel::UnlockContext>(new WalletModel::UnlockContext(walletModel->requestUnlock())) :
                    std::unique_ptr<WalletModel::UnlockContext>(nullptr);

        if( ctx.get() && !ctx->isValid() )
            return;

        CWalletTx wtx;
        if(!pwalletMain->GetProposalFeeTX(wtx, proposal.GetAddress(), proposal.GetHash(), SMARTVOTING_PROPOSAL_FEE)) {
            showErrorDialog(this, QString("Failed to create the proposal transaction. Please check the balance of the provided proposal address."));
            return;
        }

        QString questionString = tr("Are you sure you want to create the proposal?");
        questionString.append("<br /><br />Proposal fee: %1 SMART");

        SendConfirmationDialog confirmationDialog(tr("Confirm send proposal fee"),
            questionString.arg(CAmountToDouble(SMARTVOTING_PROPOSAL_FEE)), 3, QMessageBox::Question, this);
        confirmationDialog.exec();
        QMessageBox::StandardButton retval = (QMessageBox::StandardButton)confirmationDialog.result();

        if(retval != QMessageBox::Yes)
            return;

        CReserveKey reservekey(pwalletMain);
        if (!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get(), NetMsgType::TX)) {
            showErrorDialog(this,QString( "Failed to send the proposal transaction to the network! Check your connection."));
            return;
        }

        // Create the signature of the proposal hash as proof of ownership for
        // the voting portal.

        CKeyID keyID;
        if (!proposal.GetAddress().GetKeyID(keyID)){
            showErrorDialog(this,"The selected proposal address doesn't refer to a key.");
            return;
        }

        CKey key;
        if (!pwalletMain->GetKey(keyID, key)){
            showErrorDialog(this,tr("Private key for the proposal address is not available."));
            return;
        }

        CDataStream ss(SER_GETHASH, 0);
        ss << strMessageMagic;
        ss << proposal.GetHash().ToString();

        std::vector<unsigned char> vchSig;
        if (!key.SignCompact(Hash(ss.begin(), ss.end()), vchSig)){
            showErrorDialog(this,"Message signing failed.");
            return;
        }

        // Store the signed hash for register in the voting portal
        proposal.SetSignedHash(EncodeBase64(&vchSig[0], vchSig.size()));

        // Set the created tx as proposal fee tx
        proposal.SetFeeHash(wtx.GetHash());
        proposal.SetRawFeeTx(EncodeHexTx(wtx));
        // Mark the proposal as paid
        proposal.SetPaid();

        LogPrintf("SmartProposalTabWidget::publish(proposal: %s, tx: %s)\n",proposal.GetHash().ToString(), wtx.GetHash().ToString());
    }

    if( !proposal.IsPublished() ){

        PublishProposalDialog dlg(proposal);

        connect(&dlg, SIGNAL(published()), this, SLOT(published()));

        dlg.exec();
    }

    if( save() )
        updateUI();

}

void SmartProposalTabWidget::showDetails()
{
    ProposalDetailDialog dlg(proposal);
    dlg.exec();
}

void SmartProposalTabWidget::published()
{
    proposal.SetPublished();

    if( save() )
        updateUI();
}
