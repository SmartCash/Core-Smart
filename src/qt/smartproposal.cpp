
#include "smartproposal.h"
#include "ui_smartproposal.h"
#include "amount.h"
#include "guiutil.h"
#include "bitcoinunits.h"
#include "math.h"
#include "walletmodel.h"
#include "smartnode/smartnodesync.h"
#include "smartvoting/votevalidation.h"
#include "validation.h"

#include <QDesktopServices>
#include <QUrl>

SmartProposalWidget::SmartProposalWidget(const CProposal * pProposal, WalletModel *walletModel, QWidget *parent) :
    QFrame(parent),
    ui(new Ui::SmartProposalWidget),
    walletModel(walletModel)
{
    ui->setupUi(this);

    CVoteResult validResult = pProposal->GetVotingResult(VOTE_SIGNAL_VALID);
    CVoteResult fundingResult = pProposal->GetVotingResult(VOTE_SIGNAL_FUNDING);

    hash = pProposal->GetHash();
    title = QString::fromStdString(pProposal->GetTitle());
    url = QString::fromStdString(pProposal->GetUrl());
    amountSmart = 0;
    amountUSD = pProposal->GetRequestedAmount();
    createdDate = "";
    voteYesValid = validResult.nYesPower;
    voteNoValid = validResult.nNoPower;
    voteAbstainValid = validResult.nAbstainPower;
    percentYesValid = validResult.percentYes;
    percentNoValid = validResult.percentNo;
    percentAbstainValid = validResult.percentAbstain;
    voteYesFunding = fundingResult.nYesPower;
    voteNoFunding = fundingResult.nNoPower;
    voteAbstainFunding = fundingResult.nAbstainPower;
    percentYesFunding = fundingResult.percentYes;
    percentNoFunding = fundingResult.percentNo;
    percentAbstainFunding = fundingResult.percentAbstain;

    signalSelection.addButton(ui->disabledButton, 0);
    signalSelection.addButton(ui->validButton, 1);
    signalSelection.addButton(ui->fundingButton, 2);

    outcomeSelection.addButton(ui->yesButton, 0);
    outcomeSelection.addButton(ui->noButton, 1);
    outcomeSelection.addButton(ui->abstainButton, 2);

    resultSelection.addButton(ui->fundingResultButton, 0);
    resultSelection.addButton(ui->validResultButton, 1);

    votingStartHeight = pProposal->GetVotingStartHeight();

    UpdateDeadlines();

    ui->titleLabel->setText(title);

    QString smartString = QString::number(amountSmart);
    QString usdString = QString::number(amountUSD);

    AddThousandsSpaces(smartString);
    AddThousandsSpaces(usdString);

    ui->amountSmartLabel->setText(QString("%1 USD").arg(usdString));
    ui->amountUSDLabel->setText(QString("%1 SMART").arg(smartString));

    ui->yesButton->setDisabled(true);
    ui->noButton->setDisabled(true);
    ui->abstainButton->setDisabled(true);

    connect(ui->viewProposalButton, SIGNAL(clicked()),this, SLOT(viewProposal()));
    connect(ui->viewPortalButton, SIGNAL(clicked()),this, SLOT(viewPortal()));
    connect(ui->disabledButton, SIGNAL(clicked()), this, SLOT(voteButtonClicked()));
    connect(ui->validButton, SIGNAL(clicked()), this, SLOT(voteButtonClicked()));
    connect(ui->fundingButton, SIGNAL(clicked()), this, SLOT(voteButtonClicked()));
    connect(ui->yesButton, SIGNAL(clicked()), this, SLOT(voteButtonClicked()));
    connect(ui->noButton, SIGNAL(clicked()), this, SLOT(voteButtonClicked()));
    connect(ui->abstainButton, SIGNAL(clicked()), this, SLOT(voteButtonClicked()));
    connect(ui->validResultButton, SIGNAL(clicked()), this, SLOT(UpdateResult()));
    connect(ui->fundingResultButton, SIGNAL(clicked()), this, SLOT(UpdateResult()));
    connect(ui->copyHashButton, SIGNAL(clicked()), this, SLOT(copyProposalHash()));

    UpdateResult();
    UpdateVotes(pProposal);
}

void SmartProposalWidget::viewProposal(){
    QDesktopServices::openUrl(QUrl(url));
}

void SmartProposalWidget::viewPortal(){
    QDesktopServices::openUrl(QUrl("https://vote.smartcash.cc/Proposal/Details/" +
                                   QString::fromStdString(hash.ToString())));
}

void SmartProposalWidget::voteButtonClicked(){

    bool fDisableVotes = GetVoteSignal() == VOTE_SIGNAL_NONE;

    ui->yesButton->setDisabled(fDisableVotes);
    ui->noButton->setDisabled(fDisableVotes);
    ui->abstainButton->setDisabled(fDisableVotes);

    voteChanged();
}

void SmartProposalWidget::copyProposalHash()
{
    GUIUtil::setClipboard(QString::fromStdString(hash.ToString()));
}

SmartProposalWidget::~SmartProposalWidget()
{
    delete ui;
}

void SmartProposalWidget::ResetVoteSelection()
{
    ui->disabledButton->setChecked(true);
    ui->abstainButton->setChecked(true);

    voteButtonClicked();
}

vote_outcome_enum_t SmartProposalWidget::GetVoteOutcome()
{
    if( outcomeSelection.checkedId() == 0) return VOTE_OUTCOME_YES;
    if( outcomeSelection.checkedId() == 1) return VOTE_OUTCOME_NO;
    if( outcomeSelection.checkedId() == 2) return VOTE_OUTCOME_ABSTAIN;

    return VOTE_OUTCOME_NONE;
}

vote_signal_enum_t SmartProposalWidget::GetVoteSignal()
{
    if( signalSelection.checkedId() == 1) return VOTE_SIGNAL_VALID;
    if( signalSelection.checkedId() == 2) return VOTE_SIGNAL_FUNDING;

    return VOTE_SIGNAL_NONE;
}

bool SmartProposalWidget::votedValid()
{
    return ui->votedValidLabel->text() != "Nothing";
}

bool SmartProposalWidget::votedFunding()
{
    return ui->votedFundingLabel->text() != "Nothing";
}

void SmartProposalWidget::UpdateDeadlines()
{

    QString validString, fundingString;
    int validProgress, fundingProgress;

    if( !smartnodeSync.IsBlockchainSynced() || votingStartHeight == -1 ){
        validString = fundingString = "Not synced";
        validProgress = fundingProgress = 0;
    }else{

        int nHeight = chainActive.Height();
        int nBlocksDone = nHeight - votingStartHeight;

        int nMaxValidBlocks = Params().GetConsensus().nProposalValidityVoteBlocks;
        int nMaxFundingBlocks = Params().GetConsensus().nProposalFundingVoteBlocks;

        int nValidBlocksLeft = std::max<int>(nMaxValidBlocks - nBlocksDone, 0);
        int nFundingBlocksLeft = std::max<int>(nMaxFundingBlocks - nBlocksDone, 0);

        validString = QString("%1 blocks left").arg(nValidBlocksLeft);
        fundingString = QString("%1 blocks left").arg(nFundingBlocksLeft);

        validProgress = std::min<int>((static_cast<double>(nBlocksDone) / nMaxValidBlocks) * 100, 100);
        fundingProgress = std::min<int>((static_cast<double>(nBlocksDone) / nMaxFundingBlocks) * 100, 100);
    }

    ui->deadlineValidLabel->setText(validString);
    ui->deadlineValidProgress->setValue(validProgress);

    ui->deadlineFundingLabel->setText(fundingString);
    ui->deadlineFundingProgress->setValue(fundingProgress);
}

void SmartProposalWidget::UpdateResult()
{
    vote_signal_enum_t signal;

    if( resultSelection.checkedId() == 0 ){
        //Show funding results
        signal = VOTE_SIGNAL_FUNDING;
    }else{
        //Show valid results
        signal = VOTE_SIGNAL_VALID;
    }

    QString yes = QString::number(GetVoteResultAmount(signal, VOTE_OUTCOME_YES));
    QString no = QString::number(GetVoteResultAmount(signal, VOTE_OUTCOME_NO));
    QString abstain = QString::number(GetVoteResultAmount(signal, VOTE_OUTCOME_ABSTAIN));
    double yesPercent = GetVoteResultPercent(signal, VOTE_OUTCOME_YES);
    double noPercent = GetVoteResultPercent(signal, VOTE_OUTCOME_NO);
    double abstainPercent = GetVoteResultPercent(signal, VOTE_OUTCOME_ABSTAIN);

    AddThousandsSpaces(yes);
    AddThousandsSpaces(no);
    AddThousandsSpaces(abstain);

    ui->yesLabel->setText(QString("Yes %1\% ( %2 SMART )").arg(QString::number(yesPercent, 'f',2)).arg(yes));
    ui->noLabel->setText(QString("No %1\% ( %2 SMART )").arg(QString::number(noPercent, 'f',2)).arg(no));
    ui->abstainLabel->setText(QString("Abstain %1\% ( %2 SMART )").arg(QString::number(abstainPercent, 'f',2)).arg(abstain));

    ui->progressYes->setValue( (int)yesPercent );
    ui->progressNo->setValue( (int)noPercent );
    ui->progressAbstain->setValue( (int)abstainPercent );
}

void SmartProposalWidget::UpdateVotes(const CProposal *proposal)
{
    if( !walletModel ) return;

    std::set<CVoteKey> setVoteKeys;
    walletModel->VoteKeys(setVoteKeys);

    int votedYesValid = 0, votedNoValid = 0, votedAbstainValid = 0;
    int votedYesFunding = 0, votedNoFunding = 0, votedAbstainFunding = 0;

    for( auto vk : setVoteKeys ){

        vote_rec_t recVotes;
        int64_t nPower = GetVotingPower(vk);
        nPower = std::max<CAmount>(0,nPower);

        if( proposal->GetCurrentVKVotes(vk, recVotes) ){

            vote_instance_m_cit itV = recVotes.mapInstances.find(VOTE_SIGNAL_VALID);
            if( itV != recVotes.mapInstances.end() ){

                switch(itV->second.eOutcome){
                case VOTE_OUTCOME_YES:
                    votedYesValid += nPower;
                    break;
                case VOTE_OUTCOME_NO:
                    votedNoValid += nPower;
                    break;
                case VOTE_OUTCOME_ABSTAIN:
                    votedAbstainValid += nPower;
                    break;
                default: break;
                }
            }

            vote_instance_m_cit itF = recVotes.mapInstances.find(VOTE_SIGNAL_FUNDING);
            if( itF != recVotes.mapInstances.end() ){

                switch(itF->second.eOutcome){
                case VOTE_OUTCOME_YES:
                    votedYesFunding += nPower;
                    break;
                case VOTE_OUTCOME_NO:
                    votedNoFunding += nPower;
                    break;
                case VOTE_OUTCOME_ABSTAIN:
                    votedAbstainFunding += nPower;
                    break;
                default: break;
                }
            }
        }
    }

    QString votedValidString = "", votedFundingString = "";

    if( votedYesValid ){
        QString yesString = QString::number(votedYesValid);
        AddThousandsSpaces(yesString);
        votedValidString += "YES - " + yesString + "\n";
    }

    if( votedNoValid ){
        QString noString = QString::number(votedNoValid);
        AddThousandsSpaces(noString);
        votedValidString +=  "NO - " + noString + "\n";
    }

    if( votedAbstainValid ){
        QString abstainString = QString::number(votedAbstainValid);
        AddThousandsSpaces(abstainString);
        votedValidString += "ABSTAIN - " + abstainString + "\n";
    }

    if( votedYesFunding ){
        QString yesString = QString::number(votedYesFunding);
        AddThousandsSpaces(yesString);
        votedFundingString += "YES - " + yesString + "\n";
    }

    if( votedNoFunding ){
        QString noString = QString::number(votedNoFunding);
        AddThousandsSpaces(noString);
        votedFundingString +=  "NO - " + noString + "\n";
    }

    if( votedAbstainFunding ){
        QString abstainString = QString::number(votedAbstainFunding);
        AddThousandsSpaces(abstainString);
        votedFundingString += "ABSTAIN - " + abstainString + "\n";
    }

    ui->votedValidLabel->setText(votedValidString != "" ? votedValidString : "Nothing");
    ui->votedFundingLabel->setText(votedFundingString != "" ? votedFundingString : "Nothing");
}

void SmartProposalWidget::UpdateFromProposal(const CProposal *proposal)
{
    bool fThingsChanged = false;
    CVoteResult validResult = proposal->GetVotingResult(VOTE_SIGNAL_VALID);
    CVoteResult fundingResult = proposal->GetVotingResult(VOTE_SIGNAL_FUNDING);

    votingStartHeight = proposal->GetVotingStartHeight();

    if(voteYesValid != validResult.nYesPower){
        voteYesValid = validResult.nYesPower;
        fThingsChanged = true;
    }
    if(voteNoValid != validResult.nNoPower){
        voteNoValid = validResult.nNoPower;
        fThingsChanged = true;
    }
    if(voteAbstainValid != validResult.nAbstainPower){
        voteAbstainValid = validResult.nAbstainPower;
        fThingsChanged = true;
    }

    if(percentYesValid != validResult.percentYes){
        percentYesValid = validResult.percentYes;
        fThingsChanged = true;
    }
    if(percentNoValid != validResult.percentNo){
        percentNoValid = validResult.percentNo;
        fThingsChanged = true;
    }
    if(percentAbstainValid != validResult.percentAbstain){
        percentAbstainValid = validResult.percentAbstain;
        fThingsChanged = true;
    }

    if(voteYesFunding != fundingResult.nYesPower){
        voteYesFunding = fundingResult.nYesPower;
        fThingsChanged = true;
    }
    if(voteNoFunding != fundingResult.nNoPower){
        voteNoFunding = fundingResult.nNoPower;
        fThingsChanged = true;
    }
    if(voteAbstainFunding != fundingResult.nAbstainPower){
        voteAbstainFunding = fundingResult.nAbstainPower;
        fThingsChanged = true;
    }

    if(percentYesFunding != fundingResult.percentYes){
        percentYesFunding = fundingResult.percentYes;
        fThingsChanged = true;
    }
    if(percentNoFunding != fundingResult.percentNo){
        percentNoFunding = fundingResult.percentNo;
        fThingsChanged = true;
    }
    if(percentAbstainFunding != fundingResult.percentAbstain){
        percentAbstainFunding = fundingResult.percentAbstain;
        fThingsChanged = true;
    }

    if( fThingsChanged ){
        UpdateVotes(proposal);
        UpdateResult();
    }

    UpdateDeadlines();
}

int SmartProposalWidget::GetVoteResultAmount(vote_signal_enum_t signal, vote_outcome_enum_t outcome)
{
    if( signal == VOTE_SIGNAL_VALID ){

        switch(outcome){
            case VOTE_OUTCOME_YES: return voteYesValid;
            case VOTE_OUTCOME_NO: return voteNoValid;
            case VOTE_OUTCOME_ABSTAIN: return voteAbstainValid;
            default: break;
        }

    }else{

        switch(outcome){
            case VOTE_OUTCOME_YES: return voteYesFunding;
            case VOTE_OUTCOME_NO: return voteNoFunding;
            case VOTE_OUTCOME_ABSTAIN: return voteAbstainFunding;
            default: break;
        }

    }

    return 0;
}

double SmartProposalWidget::GetVoteResultPercent(vote_signal_enum_t signal, vote_outcome_enum_t outcome)
{
    if( signal == VOTE_SIGNAL_VALID ){

        switch(outcome){
            case VOTE_OUTCOME_YES: return percentYesValid;
            case VOTE_OUTCOME_NO: return percentNoValid;
            case VOTE_OUTCOME_ABSTAIN: return percentAbstainValid;
            default: break;
        }

    }else{

        switch(outcome){
            case VOTE_OUTCOME_YES: return percentYesFunding;
            case VOTE_OUTCOME_NO: return percentNoFunding;
            case VOTE_OUTCOME_ABSTAIN: return percentAbstainFunding;
            default: break;
        }

    }

    return 0;
}


