// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "votedb.h"

CProposalVoteFile::CProposalVoteFile()
    : nMemoryVotes(0),
      listVotes(),
      mapVoteIndex()
{}

CProposalVoteFile::CProposalVoteFile(const CProposalVoteFile& other)
    : nMemoryVotes(other.nMemoryVotes),
      listVotes(other.listVotes),
      mapVoteIndex()
{
    RebuildIndex();
}

void CProposalVoteFile::AddVote(const CProposalVote& vote)
{
    uint256 nHash = vote.GetHash();
    // make sure to never add/update already known votes
    if (HasVote(nHash))
        return;
    listVotes.push_front(vote);
    mapVoteIndex.emplace(nHash, listVotes.begin());
    ++nMemoryVotes;
}

bool CProposalVoteFile::HasVote(const uint256& nHash) const
{
    return mapVoteIndex.find(nHash) != mapVoteIndex.end();
}

bool CProposalVoteFile::SerializeVoteToStream(const uint256& nHash, CDataStream& ss) const
{
    vote_m_cit it = mapVoteIndex.find(nHash);
    if(it == mapVoteIndex.end()) {
        return false;
    }
    ss << *(it->second);
    return true;
}

std::vector<CProposalVote> CProposalVoteFile::GetVotes() const
{
    std::vector<CProposalVote> vecResult;
    for(vote_l_cit it = listVotes.begin(); it != listVotes.end(); ++it) {
        vecResult.push_back(*it);
    }
    return vecResult;
}

void CProposalVoteFile::RemoveVotesFromVotingKey(const CVoteKey &voteKey)
{
    vote_l_it it = listVotes.begin();
    while(it != listVotes.end()) {
        if(it->GetVoteKey() == voteKey) {
            --nMemoryVotes;
            mapVoteIndex.erase(it->GetHash());
            listVotes.erase(it++);
        }
        else {
            ++it;
        }
    }
}

void CProposalVoteFile::RebuildIndex()
{
    mapVoteIndex.clear();
    nMemoryVotes = 0;
    vote_l_it it = listVotes.begin();
    while(it != listVotes.end()) {
        CProposalVote& vote = *it;
        uint256 nHash = vote.GetHash();
        if(mapVoteIndex.find(nHash) == mapVoteIndex.end()) {
            mapVoteIndex[nHash] = it;
            ++nMemoryVotes;
            ++it;
        }
        else {
            listVotes.erase(it++);
        }
    }
}
