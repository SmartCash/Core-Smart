    // Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "specialtransactiondialog.h"
#include "ui_specialtransactiondialog.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "txmempool.h"
#include "walletmodel.h"
#include "sendcoinsdialog.h"
#include "guiconstants.h"

#include "coincontrol.h"
#include "consensus/validation.h"
#include "init.h"
#include "validation.h" // For minRelayTxFee
#include "../smartnode/instantx.h"
#include "smartvoting/votekeys.h"
#include "smartrewards/rewards.h"

#include "wallet/wallet.h"

#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFlags>
#include <QIcon>
#include <QSettings>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#define SEND_CONFIRM_DELAY 5
#define MAX_ACTIVATION_TRANSACTIONS 10

bool Error(std::string where, std::string message, QString &strError)
{
    LogPrintf("SpecialTransactionDialog::%s Error: %s\n", where, message);
    strError = QString::fromStdString(message);
    return false;
}

SpecialTransactionDialog::SpecialTransactionDialog(const SpecialTransactionType type, const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SpecialTransactionDialog),
    model(0),
    type(type),
    platformStyle(platformStyle)
{
    ui->setupUi(this);

    switch(type){
    case REGISTRATION_TRANSACTIONS:
        nRequiredFee = VOTEKEY_REGISTER_FEE;
        nRequiredNetworkFee = VOTEKEY_REGISTER_TX_FEE;
        this->setWindowTitle(strRegistrationTitle);
        ui->labelFeeDesc->setText(strRegistrationFeeDescription);
        ui->descriptionLabel->setText(strRegistrationDescription);
        break;
    case ACTIVATION_TRANSACTIONS:
        nRequiredFee = REWARDS_ACTIVATION_FEE;
        nRequiredNetworkFee = REWARDS_ACTIVATION_TX_FEE;
        this->setWindowTitle(strActivationTxTitle);
        ui->labelFeeDesc->hide();
        ui->labelFeeAmount->hide();
        ui->descriptionLabel->setText(strActivationTxDescription);
        break;
    default:
        nRequiredFee = -1;
        nRequiredNetworkFee = -1;
    }

    // context menu actions
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyAddressAction);

    // context menu signals
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showMenu(QPoint)));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));

    // click on header
#if QT_VERSION < 0x050000
    ui->treeWidget->header()->setClickable(true);
#else
    ui->treeWidget->header()->setSectionsClickable(true);
#endif
    connect(ui->treeWidget->header(), SIGNAL(sectionClicked(int)), this, SLOT(headerSectionClicked(int)));

    // ok button
    connect(ui->buttonBox, SIGNAL(clicked( QAbstractButton*)), this, SLOT(buttonBoxClicked(QAbstractButton*)));

    // automate input selection
    connect(ui->autoSelectCheckBox, SIGNAL(clicked()), this, SLOT(buttonSelectAllClicked()));

    // change coin control first column label due Qt4 bug.
    // see https://github.com/bitcoin/bitcoin/issues/5716
    ui->treeWidget->headerItem()->setText(COLUMN_CHECKBOX, QString());

    ui->treeWidget->setColumnWidth(COLUMN_CHECKBOX, 84);
    ui->treeWidget->setColumnWidth(COLUMN_AMOUNT, 100);
    ui->treeWidget->setColumnWidth(COLUMN_LABEL, 170);

    ui->treeWidget->setColumnHidden(COLUMN_TXHASH, true);         // store transaction hash in this column, but don't show it
    ui->treeWidget->setColumnHidden(COLUMN_VOUT_INDEX, true);     // store vout index in this column, but don't show it

    ui->legendLabel->setText(QString("<font color=\"%1\">Green</font> addresses are already activated. "
        "<font color=\"%2\">Yellow</font> addresses are SmartNode inputs and do not qualify for SmartRewards.")
        .arg(COLOR_GREEN.name()).arg(COLOR_YELLOW.name()));

    UpdateElements();

    // default view is sorted by amount desc
    sortView(COLUMN_AMOUNT, Qt::DescendingOrder);
}

SpecialTransactionDialog::~SpecialTransactionDialog()
{
    delete ui;
}

void SpecialTransactionDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel() && model->getAddressTableModel())
    {
        updateView();
    }
}

// ok button
void SpecialTransactionDialog::buttonBoxClicked(QAbstractButton* button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole){
        if ((type == ACTIVATION_TRANSACTIONS) && (mapOutputs.size() > MAX_ACTIVATION_TRANSACTIONS)) {
            QMessageBox::warning(this, windowTitle(),
                tr("Only %1 activation transactions can be sent at once.").arg(MAX_ACTIVATION_TRANSACTIONS),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }

        int nCount = mapOutputs.size();
        CAmount nTotalAmount = nCount * GetRequiredTotal();

        LogPrintf("SpecialTransactionDialog: Create %d transactions\n", nCount);
        for( auto it : mapOutputs ){
            LogPrintf("  %s, out: %s\n", it.first.toStdString(), it.second.ToString());
        }

        QString strType;
        switch(type){
        case REGISTRATION_TRANSACTIONS:
          strType = nCount > 1 ? "registration transactions" : "registration transaction";
          break;
        case ACTIVATION_TRANSACTIONS:
          strType = nCount > 1 ? "activation transactions" : "activation transaction";
          break;
        default:
          strType = "Unknown";
          break;
        }

        QString questionString = QString("Sending %1 %2, %3 each including fee")
                .arg(nCount)
                .arg(strType)
                .arg(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), GetRequiredTotal()));

        questionString.append("<hr />");
        questionString.append(tr("Total Amount %1")
            .arg(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), nTotalAmount)));

        SendConfirmationDialog confirmationDialog(tr("Confirm send %1").arg(strType),
            questionString, SEND_CONFIRM_DELAY, QMessageBox::Question, this);
        confirmationDialog.exec();
        QMessageBox::StandardButton retval = (QMessageBox::StandardButton)confirmationDialog.result();

        if(retval != QMessageBox::Yes) return;

        WalletModel::EncryptionStatus encVotingStatus = model->getVotingEncryptionStatus();
        WalletModel::EncryptionStatus encWalletStatus = model->getEncryptionStatus();
        bool fVotingLocked = encVotingStatus == WalletModel::Locked;
        bool fWalletLocked = encWalletStatus == WalletModel::Locked;

        std::unique_ptr<WalletModel::VotingUnlockContext> unlockVoting = fVotingLocked ?
                    std::unique_ptr<WalletModel::VotingUnlockContext>(new WalletModel::VotingUnlockContext(model->requestVotingUnlock())) :
                    std::unique_ptr<WalletModel::VotingUnlockContext>(nullptr);

        if( unlockVoting.get() && !unlockVoting->isValid() )
            return;

        std::unique_ptr<WalletModel::UnlockContext> unlockWallet = fWalletLocked ?
                    std::unique_ptr<WalletModel::UnlockContext>(new WalletModel::UnlockContext(model->requestUnlock())) :
                    std::unique_ptr<WalletModel::UnlockContext>(nullptr);

        if( unlockWallet.get() && !unlockWallet->isValid() )
            return;

        std::vector<QString> vecErrors;
        SendTransactions(vecErrors);

        if( vecErrors.size() ){
            LogPrintf("SpecialTransactionDialog: Failed to send %d %s:\n", vecErrors.size(), strType.toStdString());

            for( auto err : vecErrors){
                LogPrintf("  %s\n", err.toStdString());
            }
        }

        QString strResult;

        if( vecErrors.size() == mapOutputs.size() ){
            strResult = QString("Failed to send all %1, see debug.log for details.").arg(strType);
        }else{

            strResult = QString("Successsully sent %1 %2")
                    .arg(mapOutputs.size() - vecErrors.size())
                    .arg(strType);

            if( vecErrors.size() ){
                strResult += QString("\n\nFailed to send %1, see debug.log for details.")
                                       .arg(vecErrors.size());
            }

            strResult.append("<hr />");

            switch(type){
            case REGISTRATION_TRANSACTIONS:
                strResult.append(tr("Make sure to backup your wallet each time you register new VoteKeys. "
                                  "They are not derived from the wallet's seed so you are not able to recover them "
                                  "with any earlier backup of your wallet."));
                break;
            case ACTIVATION_TRANSACTIONS:
                strResult.append(tr("It requires %1 block confirmation for the activation transactions before the address will become eligible in the SmartRewards tab.").arg(Params().GetConsensus().nRewardsConfirmationsRequired));
                break;
            }

        }

        QMessageBox::information(this, tr("Result"),
                                       strResult,
                                       QMessageBox::Ok);

        LogPrintf("SpecialTransactionDialog: Send result %s\n", strResult.toStdString());
    }

    done(QDialog::Accepted); // closes the dialog
}

// (un)select all
void SpecialTransactionDialog::buttonSelectAllClicked()
{
    disconnect(ui->treeWidget, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(viewItemChanged(QTreeWidgetItem*, int)));

    bool fSelect = ui->autoSelectCheckBox->isChecked();

    mapOutputs.clear();

    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++){

        QTreeWidgetItem* topLevel = ui->treeWidget->topLevelItem(i);

        topLevel->setExpanded(fSelect);

        for(int k = 0; k<topLevel->childCount(); k++){
            if( !fSelect ){
                topLevel->child(k)->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
            }
        }

        if( fSelect ) selectSmallestOutput(topLevel);
        else          topLevel->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
    }

    UpdateElements();

    connect(ui->treeWidget, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(viewItemChanged(QTreeWidgetItem*, int)));
}

// context menu
void SpecialTransactionDialog::showMenu(const QPoint &point)
{
    QTreeWidgetItem *item = ui->treeWidget->itemAt(point);
    if(item)
    {
        contextMenuItem = item;

        // show context menu
        contextMenu->exec(QCursor::pos());
    }
}

// context menu action: copy address
void SpecialTransactionDialog::copyAddress()
{
    GUIUtil::setClipboard(contextMenuItem->text(COLUMN_ADDRESS));
}

// treeview: sort
void SpecialTransactionDialog::sortView(int column, Qt::SortOrder order)
{
    sortColumn = column;
    sortOrder = order;
    ui->treeWidget->sortItems(column, order);
    ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
}

// treeview: clicked on header
void SpecialTransactionDialog::headerSectionClicked(int logicalIndex)
{
    if (logicalIndex == COLUMN_CHECKBOX) // click on most left column -> do nothing
    {
        ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
    }
    else
    {
        if (sortColumn == logicalIndex)
            sortOrder = ((sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder);
        else
        {
            sortColumn = logicalIndex;
            sortOrder = ((sortColumn == COLUMN_LABEL || sortColumn == COLUMN_ADDRESS) ? Qt::AscendingOrder : Qt::DescendingOrder); // if label or address then default => asc, else default => desc
        }

        sortView(sortColumn, sortOrder);
    }
}

void SpecialTransactionDialog::selectSmallestOutput(QTreeWidgetItem* topLevel)
{
    QTreeWidgetItem* smallestItem = NULL;

    QString sAddress = topLevel->text(COLUMN_ADDRESS);

    mapOutputs.erase(sAddress);

    for (int i = 0; i < topLevel->childCount(); i++){

        QTreeWidgetItem* child = topLevel->child(i);

        child->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

        CAmount childAmount = child->data(COLUMN_AMOUNT, Qt::UserRole).toLongLong();

        if( childAmount < GetRequiredTotal() ) continue;

        if( smallestItem == NULL){
            smallestItem = child;
            continue;
        }

        CAmount nMinAmount = smallestItem->data(COLUMN_AMOUNT, Qt::UserRole).toLongLong();

        if( childAmount < nMinAmount ){
            smallestItem = child;
        }
    }

    if( smallestItem ){
        topLevel->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
        smallestItem->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
        COutPoint outpt(uint256S(smallestItem->text(COLUMN_TXHASH).toStdString()), smallestItem->text(COLUMN_VOUT_INDEX).toUInt());
        mapOutputs.insert(std::make_pair(sAddress, outpt));
    }
}

void SpecialTransactionDialog::SendTransactions(std::vector<QString> &vecErrors)
{
    vecErrors.clear();

    for( auto it : mapOutputs ){

        QString strError = "Unknown error";
        bool fSuccess = false;

        switch(type){
        case REGISTRATION_TRANSACTIONS:
            fSuccess = SendRegistration(it.first, it.second, strError);
            break;
        case ACTIVATION_TRANSACTIONS:{

            int nCurrentRound;

            {
                LOCK(cs_rewardscache);
                nCurrentRound = prewards->GetCurrentRound()->number;
            }

            fSuccess = SendActivationTransaction(it.first, it.second, nCurrentRound, strError);
        }break;
        }

        if( !fSuccess ){
            vecErrors.push_back(strError);
            continue;
        }
    }
}

bool SpecialTransactionDialog::SendRegistration(const QString &address, const COutPoint &out, QString &strError)
{
    // **
    // Check if the unspent output belongs to <address> or not
    // **

    CTransaction spendTx;
    uint256 blockHash;

    if( !GetTransaction(out.hash, spendTx, Params().GetConsensus(), blockHash, true) )
        return Error("GenerateRegistration",
              strprintf("TX-Hash %s doesn't belong to a transaction",out.hash.ToString()),
              strError);


    if( static_cast<uint32_t>(spendTx.vout.size()) - 1 < out.n )
        return Error("GenerateRegistration",
              strprintf("TX-Index %d out of range for TX %s",out.n, out.hash.ToString()),
              strError);

    const CTxOut &utxo = spendTx.vout[out.n];

    // **
    // Validate the given address
    // **

    CVoteKey voteKey;
    CSmartAddress voteAddress(address.toStdString());

    if ( !voteAddress.IsValid() )
        return Error("GenerateRegistration",
              strprintf("Failed to validate address for TX %s, index %s", out.hash.ToString(), out.n),
              strError);

    CKeyID voteAddressKeyID;

    if (!voteAddress.GetKeyID(voteAddressKeyID))
        return Error("GenerateRegistration",
              strprintf("Address does't refer to a key for TX %s, index %s", out.hash.ToString(), out.n),
              strError);

    if( GetVoteKeyForAddress(voteAddress, voteKey) )
        return Error("GenerateRegistration",
              strprintf("Address %s already registered for key: %s", voteAddress.ToString(), voteKey.ToString()),
              strError);

    std::vector<CTxDestination> addresses;
    txnouttype type;
    int nRequired;

    if (!ExtractDestinations(utxo.scriptPubKey, type, addresses, nRequired) || addresses.size() != 1) {
        return Error("GenerateRegistration",
              strprintf("Failed to extract address for output with TX %s, index %s",
                        out.hash.ToString(), out.n),
              strError);
    }

    // Force option 1 - verify the vote address with the input of the register tx
    if(  !(CSmartAddress(addresses[0]) == voteAddress) )
        return Error("GenerateRegistration",
              strprintf("Failed to force register option one for address %s with TX %s, index %s",
                        voteAddress.ToString(), out.hash.ToString(), out.n),
              strError);

    // **
    // Generate a new voting key
    // **

    CKey secret;
    secret.MakeNewKey(false);
    CVoteKeySecret voteKeySecret(secret);

    CKey vkKey = voteKeySecret.GetKey();
    if (!vkKey.IsValid())
        return Error("GenerateRegistration",
              "Voting secret key outside allowed range",
              strError);

    if( pwalletMain->HaveVotingKey(voteKeySecret.GetKey().GetPubKey().GetID()) )
        return Error("GenerateRegistration",
              strprintf("VoteKey secret exists already in the voting storage %s", voteKeySecret.ToString()),
              strError);

    CPubKey pubkey = vkKey.GetPubKey();
    if(!vkKey.VerifyPubKey(pubkey))
        return Error("GenerateRegistration",
              "Pubkey verification failed",
              strError);

    CKeyID vkKeyId = pubkey.GetID();
    voteKey.Set(vkKeyId);

    if( !voteKey.IsValid() )
        return Error("GenerateRegistration",
              "VoteKey invalid",
              strError);

    // Create the message to sign with the vote key and also voteaddress if required
    CDataStream ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << voteKey;
    ss << voteAddress;

    std::vector<unsigned char> vecSigVotekey;

    // Create the signature with the voting key
    if (!vkKey.SignCompact(Hash(ss.begin(), ss.end()), vecSigVotekey))
        return Error("GenerateRegistration",
              "Signing with VoteKey failed",
              strError);

    std::vector<unsigned char> vecData = {
        OP_RETURN_VOTE_KEY_REG_FLAG,
        0x01
    };

    CDataStream registerData(SER_NETWORK,0);

    registerData << voteKey;
    registerData << vecSigVotekey;

    vecData.insert(vecData.end(), registerData.begin(), registerData.end());

    CScript registerScript = CScript() << OP_RETURN << vecData;

    // **
    // Create the transaction
    // **

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CCoinControl coinControl;
    COutPoint output(out.hash, out.n);

    CTxDestination change = voteAddress.Get();

    coinControl.fUseInstantSend = false;
    coinControl.Select(output);
    coinControl.destChange = change;

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CWalletTx registerTx;
    CAmount nFeeRequired;
    std::string err;
    vector<CRecipient> vecSend;
    int nChangePosRet = -1;

    CRecipient recipient = {registerScript, VOTEKEY_REGISTER_FEE, false};
    vecSend.push_back(recipient);

    if (!pwalletMain->CreateTransaction(vecSend, registerTx, reservekey, nFeeRequired, nChangePosRet,
                                         err, &coinControl))
        return Error("GenerateRegistration",
              strprintf("Failed to generate transaction: %s for TX %s, index %d", err, out.hash.ToString(), out.n),
              strError);

    CValidationState state;
    if (!(CheckTransaction(registerTx, state, registerTx.GetHash(), false) || !state.IsValid()))
        return Error("GenerateRegistration",
              strprintf("Registration transaction invalid for TX %s, index %d: %s",
                        out.hash.ToString(), out.n, state.GetRejectReason()),
              strError);

    if( !pwalletMain->AddVotingKeyPubKey(voteKeySecret.GetKey(), voteKeySecret.GetKey().GetPubKey()) )
        return Error("GenerateRegistration",
              strprintf("Failed to import VoteKey secret %s", voteKeySecret.ToString()),
              strError);

    pwalletMain->mapVotingKeyRegistrations[voteAddressKeyID] = registerTx.GetHash();
    pwalletMain->mapVotingKeyMetadata[voteKeySecret.GetKey().GetPubKey().GetID()].registrationTxHash = registerTx.GetHash();

    pwalletMain->UpdateVotingKeyRegistration(voteAddressKeyID);
    pwalletMain->UpdateVotingKeyMetadata(voteKeySecret.GetKey().GetPubKey().GetID());

    if( !pwalletMain->CommitTransaction(registerTx, reservekey, g_connman.get()) )
        return Error("GenerateRegistration",
              strprintf("Failed to send the transaction TX %s", registerTx.ToString()),
              strError);

    return true;
}

bool SpecialTransactionDialog::SendActivationTransaction(const QString &address, const COutPoint &out, int nCurrentRound, QString &strError)
{
    // **
    // Check if the unspent output belongs to <address> or not
    // **

    CTransaction spendTx;
    uint256 blockHash;

    if( !GetTransaction(out.hash, spendTx, Params().GetConsensus(), blockHash, true) )
        return Error("GenerateActivation",
              strprintf("TX-Hash %s doesn't belong to a transaction",out.hash.ToString()),
              strError);

    if( static_cast<uint32_t>(spendTx.vout.size()) - 1 < out.n )
        return Error("GenerateActivation",
              strprintf("TX-Index %d out of range for TX %s",out.n, out.hash.ToString()),
              strError);

    const CTxOut &utxo = spendTx.vout[out.n];

    // **
    // Validate the given address
    // **

    CSmartAddress voteAddress(address.toStdString());

    if ( !voteAddress.IsValid() )
        return Error("GenerateActivation",
              strprintf("Failed to validate address for TX %s, index %s", out.hash.ToString(), out.n),
              strError);

    CKeyID voteAddressKeyID;

    if (!voteAddress.GetKeyID(voteAddressKeyID))
        return Error("GenerateActivation",
              strprintf("Address does't refer to a key for TX %s, index %s", out.hash.ToString(), out.n),
              strError);

    CTxDestination addressSolved;

    if (!ExtractDestination(utxo.scriptPubKey, addressSolved)) {
        return Error("GenerateActivation",
              strprintf("Failed to extract address for output with TX %s, index %s",
                        out.hash.ToString(), out.n),
              strError);
    }

    CKeyID keyIdSolved;

    // Force option 1 - verify the vote address with the input of the register tx
    if(  !CSmartAddress(addressSolved).GetKeyID(keyIdSolved) || keyIdSolved != voteAddressKeyID ){
        return Error("GenerateActivation",
              strprintf("Failed to force vote proof option one for address %s with TX %s, index %s",
                        voteAddress.ToString(), out.hash.ToString(), out.n),
              strError);
    }

    // **
    // Create the transaction
    // **

    CCoinControl coinControl;
    COutPoint output(out.hash, out.n);

    CTxDestination change = voteAddress.Get();

    coinControl.fUseInstantSend = false;
    coinControl.Select(output);
    coinControl.destChange = change;

    // Write script to self address
    CScript proofScript = GetScriptForDestination(addressSolved);


    // Figure out how much the output contains
    map<uint256, CWalletTx>::const_iterator it = pwalletMain->mapWallet.find(out.hash);
    if (it == pwalletMain->mapWallet.end())
        return Error("GenerateActivation",
              "Failed to find output transaction in wallet",
              strError);

    const CWalletTx &tx = it->second;
    CAmount nOutputAmount = tx.vout[out.n].nValue;

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CWalletTx proofTx;
    CAmount nFeeRequired;
    std::string err;
    vector<CRecipient> vecSend;
    int nChangePosRet = -1;

    CRecipient recipient = {proofScript, nOutputAmount, true};
    vecSend.push_back(recipient);

    if (!pwalletMain->CreateTransaction(vecSend, proofTx, reservekey, nFeeRequired, nChangePosRet,
                                         err, &coinControl))
        return Error("GenerateActivation",
              strprintf("Failed to generate transaction: %s for TX %s, index %d", err, out.hash.ToString(), out.n),
              strError);

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION | 0);
    ssTx << proofTx;

    CValidationState state;
    if (!(CheckTransaction(proofTx, state, proofTx.GetHash(), false) || !state.IsValid()))
        return Error("GenerateActivation",
              strprintf("Activation transaction invalid for TX %s, index %d: %s",
                        out.hash.ToString(), out.n, state.GetRejectReason()),
              strError);

    if( !pwalletMain->CommitTransaction(proofTx, reservekey, g_connman.get()) )
        return Error("GenerateActivation",
              strprintf("Failed to send the transaction TX %s", proofTx.ToString()),
              strError);

    return true;
}

// checkbox clicked by user
void SpecialTransactionDialog::viewItemChanged(QTreeWidgetItem* item, int column)
{
    disconnect(ui->treeWidget, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(viewItemChanged(QTreeWidgetItem*, int)));

    if (column == COLUMN_CHECKBOX && item->text(COLUMN_TXHASH).length() == 64) // transaction hash is 64 characters (this means its a child node, so its not a parent node in tree mode)
    {
        ui->autoSelectCheckBox->setChecked(false);

        QString sAddress = item->parent()->text(COLUMN_ADDRESS);
        mapOutputs.erase(sAddress);

        COutPoint outpt(uint256S(item->text(COLUMN_TXHASH).toStdString()), item->text(COLUMN_VOUT_INDEX).toUInt());

        if (item->isDisabled()) // locked (this happens if "check all" through parent node)
            item->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        else if(item->checkState(COLUMN_CHECKBOX) == Qt::Checked)
            mapOutputs.insert(std::make_pair(sAddress, outpt));

        bool fUncheckOthers = item->checkState(COLUMN_CHECKBOX) == Qt::Checked;

        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++){

            QTreeWidgetItem* topLevel = ui->treeWidget->topLevelItem(i);

            if( !topLevel->childCount() || topLevel != item->parent() )
                continue;

            for (int k = 0; k < topLevel->childCount(); k++){

                if( topLevel->child(k) != item && fUncheckOthers ){
                    topLevel->child(k)->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
                }

            }
        }

        if( fUncheckOthers )
            item->parent()->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
        else
            item->parent()->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

    }else if(column == COLUMN_CHECKBOX){

        bool fUnckeckChilds = item->checkState(COLUMN_CHECKBOX) == Qt::Unchecked;

        for(int i = 0; i<item->childCount(); i++){
            if( fUnckeckChilds ){
                item->child(i)->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
            }
        }

        if( fUnckeckChilds ){
            QString sAddress = item->text(COLUMN_ADDRESS);
            mapOutputs.erase(sAddress);
        }else{
            selectSmallestOutput(item);
        }

        item->setExpanded(!fUnckeckChilds);
    }

    // TODO: Remove this temporary qt5 fix after Qt5.3 and Qt5.4 are no longer used.
    //       Fixed in Qt5.5 and above: https://bugreports.qt.io/browse/QTBUG-43473
#if QT_VERSION >= 0x050000
    else if (column == COLUMN_CHECKBOX && item->childCount() > 0)
    {
        if (item->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked && item->child(0)->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked)
            item->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
    }
#endif

    UpdateElements();

    connect(ui->treeWidget, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(viewItemChanged(QTreeWidgetItem*, int)));

}

void SpecialTransactionDialog::UpdateElements()
{

    if( !model ) return;

    ui->buttonBox->button(QDialogButtonBox::Ok)->setDisabled(!mapOutputs.size());
    ui->labelAddressCount->setText(QString::number(mapOutputs.size()));

    auto displayUnit = model->getOptionsModel()->getDisplayUnit();
    ui->labelFeeAmount->setText(tr("%1")
        .arg(BitcoinUnits::formatHtmlWithUnit(displayUnit, nRequiredFee * mapOutputs.size() )));
    ui->labelNetworkFee->setText(tr("%1")
        .arg(BitcoinUnits::formatHtmlWithUnit(displayUnit, nRequiredNetworkFee * mapOutputs.size() )));
    ui->labelInputAmount->setText(tr("%1")
        .arg(BitcoinUnits::formatHtmlWithUnit(displayUnit, GetRequiredTotal() * mapOutputs.size() )));
}

void SpecialTransactionDialog::updateView()
{
    if (!model || !model->getOptionsModel() || !model->getAddressTableModel())
        return;

    disconnect(ui->treeWidget, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(viewItemChanged(QTreeWidgetItem*, int)));

    ui->treeWidget->clear();
    ui->treeWidget->setEnabled(false); // performance, otherwise updateLabels would be called for every checked checkbox
    ui->treeWidget->setAlternatingRowColors(true);
    QFlags<Qt::ItemFlag> flgCheckbox = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;

    int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    std::map<QString, std::vector<COutput> > mapCoins;
    model->listCoins(mapCoins, false);

    BOOST_FOREACH(const PAIRTYPE(QString, std::vector<COutput>)& coins, mapCoins) {
        QBrush lineBrush;
        QString sWalletAddress = coins.first;
        QString sWalletLabel = model->getAddressTableModel()->labelForAddress(sWalletAddress);

        if( type == REGISTRATION_TRANSACTIONS ){
            CSmartAddress voteAddress(sWalletAddress.toStdString());
            CKeyID voteAddressKeyId;

            // Step over if the address is already registered
            if( IsRegisteredForVoting(voteAddress) ) continue;

            // Or if there is already a registration hash set for this address
            // Happens if the registration is sent but not confirmed and registered
            if( voteAddress.GetKeyID(voteAddressKeyId) &&
                 !pwalletMain->mapVotingKeyRegistrations[voteAddressKeyId].IsNull() ){
                continue;
            }

        }

        if( type == ACTIVATION_TRANSACTIONS ){
            CKeyID keyId;
            CSmartAddress voteAddress(sWalletAddress.toStdString());
            int nCurrentRound = 0;
            CSmartRewardEntry *reward = nullptr;

            {
                LOCK(cs_rewardscache);
                nCurrentRound = prewards->GetCurrentRound()->number;
                prewards->GetRewardEntry(voteAddress, reward, false);
            }

            if( !voteAddress.GetKeyID(keyId) ){
                continue;
            }

            if (reward) {
                if (reward->fActivated) {
                    // Address is already activated
                    lineBrush.setColor(COLOR_GREEN);
                } else if (!reward->smartnodePaymentTx.IsNull()) {
                    // Address is linked to a SmartNode
                    lineBrush.setColor(COLOR_YELLOW);
                }
            }
        }

        CCoinControlWidgetItem *itemWalletAddress = new CCoinControlWidgetItem();
        itemWalletAddress->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

        if (sWalletLabel.isEmpty()){

            LOCK(pwalletMain->cs_wallet);
            const COutput& out = coins.second[0];
            if( pwalletMain->IsChange(out.tx->vout[out.i]) ){
                sWalletLabel = tr("(change)");
            }else{
                sWalletLabel = tr("(no label)");
            }
        }

        // wallet address
        ui->treeWidget->addTopLevelItem(itemWalletAddress);

        itemWalletAddress->setFlags(flgCheckbox);
        itemWalletAddress->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

        // label
        itemWalletAddress->setForeground(COLUMN_LABEL, lineBrush);
        itemWalletAddress->setText(COLUMN_LABEL, sWalletLabel);

        // address
        itemWalletAddress->setForeground(COLUMN_ADDRESS, lineBrush);
        itemWalletAddress->setText(COLUMN_ADDRESS, sWalletAddress);

        CAmount nSum = 0;
        int nChildren = 0;
        BOOST_FOREACH(const COutput& out, coins.second) {

            nSum += out.tx->vout[out.i].nValue;
            nChildren++;

            CCoinControlWidgetItem *itemOutput;
            itemOutput = new CCoinControlWidgetItem(itemWalletAddress);
            itemOutput->setFlags(flgCheckbox);
            itemOutput->setCheckState(COLUMN_CHECKBOX,Qt::Unchecked);

            // amount
            itemOutput->setText(COLUMN_AMOUNT, BitcoinUnits::format(nDisplayUnit, out.tx->vout[out.i].nValue));
            itemOutput->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong)out.tx->vout[out.i].nValue)); // padding so that sorting works correctly

            // transaction hash
            uint256 txhash = out.tx->GetHash();
            itemOutput->setText(COLUMN_TXHASH, QString::fromStdString(txhash.GetHex()));

            // vout index
            itemOutput->setText(COLUMN_VOUT_INDEX, QString::number(out.i));

             // disable locked coins
            if (model->isLockedCoin(txhash, out.i))
            {
                itemOutput->setDisabled(true);
                itemOutput->setIcon(COLUMN_CHECKBOX, platformStyle->SingleColorIcon(":/icons/lock_closed"));
            }

            // disable too small coins
           if (itemOutput->data(COLUMN_AMOUNT, Qt::UserRole).toLongLong() < GetRequiredTotal() )
           {
               itemOutput->setDisabled(true);
           }
        }

        itemWalletAddress->setText(COLUMN_CHECKBOX, "(" + QString::number(nChildren) + ")");
        itemWalletAddress->setForeground(COLUMN_AMOUNT, lineBrush);
        itemWalletAddress->setText(COLUMN_AMOUNT, BitcoinUnits::format(nDisplayUnit, nSum));
        itemWalletAddress->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong)nSum));
    }

    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
        if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked)
            ui->treeWidget->topLevelItem(i)->setExpanded(true);

    // sort view
    sortView(sortColumn, sortOrder);
    ui->treeWidget->setEnabled(true);

    UpdateElements();

    connect(ui->treeWidget, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(viewItemChanged(QTreeWidgetItem*, int)));
}
