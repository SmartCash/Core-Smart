// Copyright (c) 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartvoting/proposal.h"
#include "validation.h"

#include <regex>

const CAmount nProposalFee = 1 * COIN;

const size_t nProposalTitleLengthMin = 10;
const size_t nProposalTitleLengthMax = 200;

const int64_t nProposalMilestoneDistanceMax = 60; // 60 days
const size_t nProposalMilestoneDescriptionLengthMin = 10;
const size_t nProposalMilestoneDescriptionLengthMax = 100;

static bool char_isspace(char c) {
    return std::isspace(static_cast<unsigned char>(c));
}


bool CProposal::ValidateTitle(const string& strTitle, string& strError)
{
    strError.clear();

    std::string strClean = strTitle;

    strClean.erase(std::remove_if(strClean.begin(), strClean.end(), char_isspace), strClean.end());

    if( strClean.length() < nProposalTitleLengthMin)
        strError = strprintf("Title too short. Minimum required: %d characters (whitespaces excluded).",nProposalTitleLengthMin);
    else if( strClean.length() > nProposalTitleLengthMax )
        strError = strprintf("Title too long. Maximum allowed: %d characters (whitespaces excluded).",nProposalTitleLengthMax);

    return strError.empty();
}

bool CProposal::ValidateUrl(const string& strUrl, string& strError)
{
    std::regex ex("(http|https)://([^/ :]+):?([^/ ]*)(/?[^ #?]*)\\x3f?([^ #]*)#?([^ ]*)");

    strError.clear();

    if( !std::regex_match(strUrl, ex) ){
        strError = "Invalid URL: Required format http(s)://<url>";
        return false;
    }

    return true;
}

bool CProposal::IsTitleValid(string& strError)
{
    return ValidateTitle(title, strError);
}

bool CProposal::IsUrlValid(string& strError)
{
    return ValidateUrl(url, strError);
}

bool CProposal::IsAddressValid(string& strError)
{
    strError.clear();

    if( !address.IsValid() ){
        strError = "Invalid SmartCash address";
        return false;
    }

    return true;
}

bool CProposal::IsMilestoneVectorValid(std::string& strError)
{
    strError.clear();

    if( !vecMilestones.size() )
        strError = "At least 1 milestone required";
    else{
        CProposalMilestone distanceCheck;

        for( size_t i = 0; i<vecMilestones.size();i++){
            std::string strErrTmp;

            if( i == 0 ){
                distanceCheck = vecMilestones.at(i);
            }else{

                int64_t distance = vecMilestones.at(i).GetTime() - distanceCheck.GetTime();
                if( distance > nProposalMilestoneDistanceMax * 24 * 60 * 60 ){
                    strError += strprintf("Milestone #%d: Maximum milestone length is %d days\n", i, nProposalMilestoneDistanceMax);
                }

                distanceCheck = vecMilestones.at(i);
            }

            if( !vecMilestones.at(i).IsDescriptionValid(strErrTmp) ){
                strError += strprintf("Milestone #%d: %s\n", i, strErrTmp);
            }

        }
    }

    return strError.empty();
}

bool CProposal::IsValid(std::string& strError)
{
    bool fValid = true;
    std::string strErr1, strErr2, strErr3, strErr4;
    fValid &= IsTitleValid(strErr1);
    fValid &= IsUrlValid(strErr2);
    fValid &= IsAddressValid(strErr3);
    fValid &= IsMilestoneVectorValid(strErr4);

    if( !fValid ){
        strError = strErr1 +
                   "\n" + strErr2 +
                   "\n" + strErr3 +
                   "\n" + strErr4;
    }else{
        strError.clear();
    }

    return fValid;
}

uint256 CProposal::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);

    ss << nTimeCreated;
    ss << title;
    ss << url;
    ss << address;
    ss << vecMilestones;

    return ss.GetHash();
}

void CInternalProposal::AddMilestone(CProposalMilestone &milestone)
{
    vecMilestones.push_back(milestone);
    std::sort(vecMilestones.begin(), vecMilestones.end());
}

void CInternalProposal::RemoveMilestone(size_t index)
{
    if( index < vecMilestones.size() )
        vecMilestones.erase(vecMilestones.begin() + index);

    std::sort(vecMilestones.begin(), vecMilestones.end());
}

bool CProposalMilestone::IsDescriptionValid(string &strError)
{
    strError.clear();

    std::string strClean = strDescription;

    strClean.erase(std::remove_if(strClean.begin(), strClean.end(), char_isspace), strClean.end());

    if( strClean.length() < nProposalMilestoneDescriptionLengthMin)
        strError = strprintf("Description too short. Minimum required: %d characters (whitespaces excluded).",nProposalMilestoneDescriptionLengthMin);
    else if( strClean.length() > nProposalMilestoneDescriptionLengthMax )
        strError = strprintf("Description too long. Maximum allowed: %d characters (whitespaces excluded).",nProposalMilestoneDescriptionLengthMax);

    return strError.empty();
}
