// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartrewards/rewards.h"
#include "consensus/consensus.h"
#include "init.h"
#include "rewards.h"
#include "script/standard.h"
#include "smarthive/hive.h"
#include "smartnode/smartnodepayments.h"
#include "smartnode/spork.h"
#include "smartrewards/rewardspayments.h"
#include "ui_interface.h"
#include "validation.h"

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/range/irange.hpp>
#include <boost/thread.hpp>

#include <fstream>
#include <iostream>

#define REWARDS_MAX_CACHE 400000000UL // 400MB

CSmartRewards* prewards = NULL;

CCriticalSection cs_rewardsdb;
CCriticalSection cs_rewardscache;

size_t nCacheRewardEntries;

// Used for time conversions.
boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));

struct CompareRewardScore {
    bool operator()(const std::pair<arith_uint256, CSmartRewardResultEntry*>& s1,
        const std::pair<arith_uint256, CSmartRewardResultEntry*>& s2) const
    {
        return (s1.first != s2.first) ? (s1.first < s2.first) : (s1.second->entry.balance < s2.second->entry.balance);
    }
};

struct ComparePaymentPrtList {
    bool operator()(const CSmartRewardResultEntry* p1,
        const CSmartRewardResultEntry* p2) const
    {
        return *p1 < *p2;
    }
};

// Estimate or return the current block height.
int GetBlockHeight(const CBlockIndex* index)
{
    int64_t syncDiff = std::time(0) - index->GetBlockTime();
    int64_t firstTxDiff;
    if (MainNet())
        firstTxDiff = std::time(0) - nStartRewardTime; // Diff from the reward blocks start till now on mainnet.
    else
        firstTxDiff = std::time(0) - nFirstTxTimestamp_Testnet; // Diff from the first transaction till now on testnet.
    return syncDiff > 1200 ? firstTxDiff / 55 : index->nHeight; // If we are 20 minutes near now use the current height.
}

bool ExtractDestination(const CScript& scriptPubKey, CSmartAddress& idRet)
{
    vector<vector<unsigned char> > vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_PUBKEY) {
        CPubKey pubKey(vSolutions[0]);
        if (!pubKey.IsValid())
            return false;

        idRet = CSmartAddress(pubKey.GetID());
        return true;
    } else if (whichType == TX_PUBKEYHASH) {
        idRet = CSmartAddress(CKeyID(uint160(vSolutions[0])));
        return true;
    } else if (whichType == TX_SCRIPTHASH) {
        idRet = CSmartAddress(CScriptID(uint160(vSolutions[0])));
        return true;
    } else if (whichType == TX_PUBKEYHASHLOCKED) {
        idRet = CSmartAddress(CKeyID(uint160(vSolutions[0])));
        return true;
    } else if (whichType == TX_SCRIPTHASHLOCKED) {
        idRet = CSmartAddress(CScriptID(uint160(vSolutions[0])));
        return true;
    }
    // Multisig txns have more than one address...
    return false;
}

bool CSmartRewards::Verify()
{
    LOCK(cs_rewardsdb);
    int nHeight = cache.GetCurrentBlock()->nHeight;
    return pdb->Verify(nHeight);
}

bool CSmartRewards::NeedsCacheWrite()
{
    return cache.NeedsSync();
}

void CSmartRewards::UpdateRoundPayoutParameter()
{
    AssertLockHeld(cs_rewardscache);

    const CSmartRewardRound* round = cache.GetCurrentRound();
    int64_t nBlockPayees = round->nBlockPayees, nBlockInterval;

    int64_t nPayeeCount = round->eligibleEntries - round->disqualifiedEntries;
    int nFirst_1_3_Round = Params().GetConsensus().nRewardsFirst_1_3_Round;

    if (round->number < nFirst_1_3_Round) {
        nBlockPayees = Params().GetConsensus().nRewardsPayouts_1_2_BlockPayees;
        nBlockInterval = Params().GetConsensus().nRewardsPayouts_1_2_BlockInterval;
    } else if (nPayeeCount) {
        int64_t nBlockStretch = Params().GetConsensus().nRewardsPayouts_1_3_BlockStretch;
        int64_t nBlocksPerRound = Params().GetConsensus().nRewardsBlocksPerRound_1_3;
        int64_t nTempBlockPayees = Params().GetConsensus().nRewardsPayouts_1_3_BlockPayees;

        nBlockPayees = std::max<int>(nTempBlockPayees, (nPayeeCount / nBlockStretch * nTempBlockPayees) + 1);

        if (nPayeeCount > nBlockPayees) {
            int64_t nStartDelayBlocks = Params().GetConsensus().nRewardsPayoutStartDelay;
            int64_t nBlocksTarget = nStartDelayBlocks + nBlocksPerRound;
            nBlockInterval = ((nBlockStretch * nBlockPayees) / nPayeeCount) + 1;
            int64_t nStretchedLength = nPayeeCount / nBlockPayees * (nBlockInterval);

            if (nStretchedLength > nBlocksTarget) {
                nBlockInterval--;
            } else if (nStretchedLength < nBlockStretch) {
                nBlockInterval++;
            }

        } else {
            // If its only one block to pay
            nBlockInterval = 1;
        }

    } else {
        // If there are no eligible smartreward entries
        nBlockPayees = 0;
        nBlockInterval = 0;
    }

    cache.UpdateRoundPayoutParameter(nBlockPayees, nBlockInterval);
}

void CSmartRewards::UpdatePercentage()
{
    AssertLockHeld(cs_rewardscache);

    const CSmartRewardRound* currentRound = cache.GetCurrentRound();

    double nPercent = 0.0;
    /*    if (Is_1_3(currentRound->number)) {
        if (currentRound->eligibleSmart > 0)
            nPercent = double(currentRound->rewards) / currentRound->eligibleSmart;
    } else {
*/
    if ((currentRound->eligibleSmart - currentRound->disqualifiedSmart) > 0) {
        nPercent = double(currentRound->rewards) / (currentRound->eligibleSmart - currentRound->disqualifiedSmart);
    }

    cache.UpdateRoundPercent(nPercent);
}

CAmount CSmartRewards::GetAddressBalanceAtRound(const CSmartAddress& address, int16_t round)
{
    CSmartRewardResultEntryList roundResults;
    if (!GetRewardRoundResults(round, roundResults)) {
        return 0;
    }

    auto addressResult = std::find_if(roundResults.begin(), roundResults.end(),
        [&address](const CSmartRewardResultEntry& e) -> bool {
            return address == e.entry.id;
        });

    if (addressResult == roundResults.end()) {
        return 0;
    }
    if (addressResult->entry.balance < 0)
        addressResult->entry.balance = 0;
    return addressResult->entry.balance;
}
/*
void CSmartRewards::SaveToCacheEachRewardEntry(CSmartRewardEntryMap& smartRewardEntriesFromDB)
{
    for (auto itDb = smartRewardEntriesFromDB.begin(); itDb != smartRewardEntriesFromDB.end(); ++itDb) {
        auto itCache = cache.GetEntries()->find(itDb->first);

        if (itCache == cache.GetEntries()->end()) {
            cache.AddEntry(itDb->second);
        } else {
            delete itDb->second;
        }
    }
}*/
bool CSmartRewards::Is_1_3(uint16_t currentRoundNumber)
{
    return currentRoundNumber >= Params().GetConsensus().nRewardsFirst_1_3_Round;
}
CAmount CSmartRewards::CalculateWeightedBalance(CSmartAddress address, CSmartRewardEntry* smartRewardEntry, uint16_t currentRoundNumber)
{
    int nFirst_1_3_Round = Params().GetConsensus().nRewardsFirst_1_3_Round;
    smartRewardEntry->balanceEligible = smartRewardEntry->balance;
    // Balance 2 months ago
    if (currentRoundNumber > (nFirst_1_3_Round + 8) && GetAddressBalanceAtRound(address, currentRoundNumber - 1) > 9999 * COIN) {
        smartRewardEntry->balanceEligible += GetAddressBalanceAtRound(address, currentRoundNumber - 8);
        // Balance 4 months ago
        if (currentRoundNumber > (nFirst_1_3_Round + 16)) {
            smartRewardEntry->balanceEligible += 2 * GetAddressBalanceAtRound(address, currentRoundNumber - 16);
            // Balance 6 months ago
            if (currentRoundNumber > (nFirst_1_3_Round + 26)) {
                smartRewardEntry->balanceEligible += 2 * GetAddressBalanceAtRound(address, currentRoundNumber - 26);
            }
        }
    }
    return smartRewardEntry->balanceEligible;
}
void CSmartRewards::EvaluateRound(CSmartRewardRound& nextRewardRound)
{
    LOCK(cs_rewardscache);

    UpdateRoundPayoutParameter();

    const CSmartRewardRound* currentRound = cache.GetCurrentRound();

    int nFirst_1_3_Round = Params().GetConsensus().nRewardsFirst_1_3_Round;

    CAmount nMinBalance = nextRewardRound.number < nFirst_1_3_Round ? SMART_REWARDS_MIN_BALANCE_1_2 : SMART_REWARDS_MIN_BALANCE_1_3;

    CSmartRewardsRoundResult* pCurrentSmartRewardResult = new CSmartRewardsRoundResult();

    pCurrentSmartRewardResult->round = *currentRound;
    pCurrentSmartRewardResult->round.UpdatePayoutParameter();

    CAmount nReward;
    CSmartRewardEntryMap smartRewardEntriesFromDB;
    pdb->ReadRewardEntries(smartRewardEntriesFromDB);

    //    SaveToCacheEachRewardEntry(smartRewardEntriesFromDB);

    for (auto itDb = smartRewardEntriesFromDB.begin(); itDb != smartRewardEntriesFromDB.end(); ++itDb) {
        auto itCache = cache.GetEntries()->find(itDb->first);

        if (itCache == cache.GetEntries()->end()) {
            cache.AddEntry(itDb->second);
        } else {
            delete itDb->second;
        }
    }

    auto smartRewardEntry = cache.GetEntries()->begin();
    while (smartRewardEntry != cache.GetEntries()->end()) {
        // Only compute rewards if not an Hive address and balance is over the minimum
        if (currentRound->number && smartRewardEntry->second->balance >= nMinBalance && !SmartHive::IsHive(smartRewardEntry->second->id)) {
            // If prior to 1.3, just use the balance as eligible
            if (!Is_1_3(currentRound->number)) {
                smartRewardEntry->second->balanceEligible = smartRewardEntry->second->balance;
                // If we pass 1.3 start round, calculate the weighted balance.
            } else if (Is_1_3(currentRound->number) && smartRewardEntry->second->IsEligible()) {
                smartRewardEntry->second->balanceEligible = CalculateWeightedBalance(smartRewardEntry->first, smartRewardEntry->second, currentRound->number);
            }
            if (smartRewardEntry->second->fDisqualifyingTx) {
                nReward = 0;
                smartRewardEntry->second->balanceEligible = 0;
            }
            if (smartRewardEntry->second->balanceEligible > 0) {
                ++nextRewardRound.eligibleEntries;
                nextRewardRound.eligibleSmart += smartRewardEntry->second->balanceEligible;
            }
        }
        ++smartRewardEntry;
    }
    UpdatePercentage();
    auto smartRewardEntry2 = cache.GetEntries()->begin();
    while (smartRewardEntry2 != cache.GetEntries()->end()) {
        nReward = smartRewardEntry2->second->balanceEligible > 0 && !smartRewardEntry2->second->fDisqualifyingTx ? CAmount(smartRewardEntry2->second->balanceEligible * currentRound->percent) : 0;

        if (nReward > 0) {
            pCurrentSmartRewardResult->results.push_back(new CSmartRewardResultEntry(smartRewardEntry2->second, nReward));
            pCurrentSmartRewardResult->payouts.push_back(pCurrentSmartRewardResult->results.back());
        }

        //        smartRewardEntry2->second->balanceAtStart = smartRewardEntry2->second->balance;

        // Reset disqualifying and SmartNode transactions each cycle.
        smartRewardEntry2->second->disqualifyingTx.SetNull();
        smartRewardEntry2->second->fDisqualifyingTx = false;
        smartRewardEntry2->second->smartnodePaymentTx.SetNull();
        smartRewardEntry2->second->fSmartnodePaymentTx = false;
        // Prior to first 1.3 round clear voteproven
        /*        if (currentRound->number == (nFirst_1_3_Round - 1)) {
            smartRewardEntry2->second->voteProof.SetNull();
            smartRewardEntry2->second->fVoteProven = false;
        }
*/
        ++smartRewardEntry2;
    }

    if (pCurrentSmartRewardResult->payouts.size()) {
        if (!Is_1_3(currentRound->number)) {
            // Sort it to make sure the slices are the same network wide.
            std::sort(pCurrentSmartRewardResult->payouts.begin(), pCurrentSmartRewardResult->payouts.end(), ComparePaymentPrtList());
        } else {
            uint256 blockHash;
            if (!GetBlockHash(blockHash, pCurrentSmartRewardResult->round.startBlockHeight)) {
                throw std::runtime_error(strprintf("CSmartRewards::EvaluateRound -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", currentRound->startBlockHeight));
            }

            std::vector<std::pair<arith_uint256, CSmartRewardResultEntry*> > vecScores;
            // Since we use payouts stretched out over a week better to have some "random" sort here
            // based on a score calculated with the round start's blockhash.
            CSmartRewardResultEntryPtrList::iterator it = pCurrentSmartRewardResult->payouts.begin();

            while (it != pCurrentSmartRewardResult->payouts.end()) {
                arith_uint256 nScore = (*it)->CalculateScore(blockHash);
                vecScores.push_back(std::make_pair(nScore, *it));
                ++it;
            }

            std::sort(vecScores.begin(), vecScores.end(), CompareRewardScore());

            pCurrentSmartRewardResult->payouts.clear();

            for (auto s : vecScores)
                pCurrentSmartRewardResult->payouts.push_back(s.second);
        }
    }

    // Calculate the current cycle rewards to pay.
    int64_t nTime = GetTime();
    int64_t nStartHeight = nextRewardRound.startBlockHeight;
    double dBlockReward = nextRewardRound.number < nFirst_1_3_Round ? 0.15 : 0.60;
    nextRewardRound.rewards = 0;

    while (nStartHeight <= nextRewardRound.endBlockHeight)
        nextRewardRound.rewards += GetBlockValue(nStartHeight++, 0, nTime) * dBlockReward;

    if (pCurrentSmartRewardResult->round.number) {
        cache.AddFinishedRound(pCurrentSmartRewardResult->round);
    }

    cache.SetResult(pCurrentSmartRewardResult);
    cache.SetCurrentRound(nextRewardRound);
}

bool CSmartRewards::GetRewardRoundResults(const int16_t round, CSmartRewardResultEntryList& results)
{
    LOCK(cs_rewardsdb);
    return pdb->ReadRewardRoundResults(round, results);
}

bool CSmartRewards::GetRewardRoundResults(const int16_t round, CSmartRewardResultEntryPtrList& results)
{
    LOCK(cs_rewardsdb);
    return pdb->ReadRewardRoundResults(round, results);
}

const CSmartRewardsRoundResult* CSmartRewards::GetLastRoundResult()
{
    return cache.GetLastRoundResult();
}

bool CSmartRewards::GetRewardPayouts(const int16_t round, CSmartRewardResultEntryList& payouts)
{
    LOCK(cs_rewardsdb);
    return pdb->ReadRewardPayouts(round, payouts);
}

bool CSmartRewards::GetRewardPayouts(const int16_t round, CSmartRewardResultEntryPtrList& payouts)
{
    LOCK(cs_rewardsdb);
    return pdb->ReadRewardPayouts(round, payouts);
}

bool CSmartRewards::GetRewardEntry(const CSmartAddress& id, CSmartRewardEntry*& entry, bool fCreate)
{
    LOCK(cs_rewardscache);

    // Return the entry if its already in cache.
    auto it = cache.GetEntries()->find(id);

    if (it != cache.GetEntries()->end()) {
        entry = it->second;
        return true;
    }

    entry = new CSmartRewardEntry(id);

    // Return the entry if its already in db.
    if (pdb->ReadRewardEntry(id, *entry)) {
        cache.AddEntry(entry);
        return true;
    }

    if (fCreate) {
        cache.AddEntry(entry);
        return true;
    }

    delete entry;

    return false;
}

bool CSmartRewards::GetRewardEntries(CSmartRewardEntryMap& entries)
{
    LOCK(cs_rewardsdb);
    return pdb->ReadRewardEntries(entries);
}

bool CSmartRewards::SyncCached()
{
    LOCK2(cs_rewardscache, cs_rewardsdb);

    int nTimeStart = GetTimeMicros();
    int nEntriesPre = cache.GetEntries()->size();
    int nSizePre = cache.EstimatedSize();

    bool ret = pdb->SyncCached(cache);

    cache.Clear();

    int nTimeDone = GetTimeMicros();

    int nEntriesPost = cache.GetEntries()->size();
    int nSizePost = cache.EstimatedSize();

    LogPrint("smartrewards-bench", "CSmartRewards::SyncCached size before/after %dMB/%dMB, entries before/after %d/%d, time %.2fms", nSizePre / 1000000, nSizePost / 1000000, nEntriesPre, nEntriesPost, (nTimeDone - nTimeStart) * 0.001);

    return ret;
}

bool CSmartRewards::IsSynced()
{
    static bool fSynced = false;

    if (fSynced)
        return true;

    fSynced = (GetTime() - cache.GetCurrentBlock()->nTime) <= nRewardsSyncDistance;

    return fSynced;
}

int CSmartRewards::GetBlocksPerRound(const int nRound)
{
    const CChainParams& chainparams = Params();

    if (nRound < chainparams.GetConsensus().nRewardsFirst_1_3_Round) {
        return chainparams.GetConsensus().nRewardsBlocksPerRound_1_2;
    } else {
        return chainparams.GetConsensus().nRewardsBlocksPerRound_1_3;
    }
}

CSmartRewards::CSmartRewards(CSmartRewardsDB* prewardsdb) : pdb(prewardsdb)
{
    LOCK2(cs_rewardscache, cs_rewardsdb);

    CSmartRewardBlock block;
    CSmartRewardRound round;
    CSmartRewardRoundMap rounds;
    CSmartRewardResultEntryList results;

    pdb->ReadLastBlock(block);
    pdb->ReadCurrentRound(round);
    pdb->ReadRounds(rounds);

    cache.Load(block, round, rounds);

    CSmartRewardsRoundResult* pResult = new CSmartRewardsRoundResult();

    if (round.number > 1) {
        pResult->fSynced = true;
        pResult->round = rounds[round.number - 1];
        pdb->ReadRewardRoundResults(round.number - 1, pResult->results);

        for (auto it : pResult->results) {
            if (it->reward) {
                pResult->payouts.push_back(it);
            }
        }
    }

    cache.SetResult(pResult);

    LogPrintf("CSmartRewards::CSmartRewards\n  Last block %s\n  Current Round %s\n  Rounds: %d", block.ToString(), round.ToString(), rounds.size());
}

bool CSmartRewards::GetLastBlock(CSmartRewardBlock& block)
{
    LOCK(cs_rewardsdb);
    // Read the last block stored in the rewards database.
    return pdb->ReadLastBlock(block);
}

bool CSmartRewards::GetTransaction(const uint256 nHash, CSmartRewardTransaction& transaction)
{
    LOCK2(cs_rewardscache, cs_rewardsdb);
    // If the transaction is already in the cache use this one.
    auto it = cache.GetAddedTransactions()->find(nHash);

    if (it != cache.GetAddedTransactions()->end()) {
        transaction = it->second;
        return true;
    }

    return pdb->ReadTransaction(nHash, transaction);
}

const CSmartRewardRound* CSmartRewards::GetCurrentRound()
{
    return cache.GetCurrentRound();
}

const CSmartRewardRoundMap* CSmartRewards::GetRewardRounds()
{
    return cache.GetRounds();
}

void CSmartRewards::ProcessInput(const CTransaction& tx, const CTxOut& in, CSmartAddress** voteProofCheck, CAmount& nVoteProofIn, uint16_t nCurrentRound, CSmartRewardsUpdateResult& result)
{
    CSmartRewardEntry* rEntry = nullptr;
    CSmartAddress id;

    if (!ExtractDestination(in.scriptPubKey, id)) {
        LogPrint("smartrewards-tx", "CSmartRewards::ProcessInput - Could't parse CSmartAddress: %s\n", in.ToString());
        return;
    }

    if (!GetRewardEntry(id, rEntry, false)) {
        LogPrint("smartrewards-tx", "CSmartRewards::ProcessInput - Spend without previous receive - %s", tx.ToString());
        return;
    }
    if (Is_1_3(nCurrentRound) && tx.IsVoteProof()) {
        rEntry->balance += in.nValue;
    }
    rEntry->balance -= in.nValue;

    if (Is_1_3(nCurrentRound) && !rEntry->fDisqualifyingTx) {
        if (rEntry->IsEligible()) {
            result.disqualifiedEntries++;
            result.disqualifiedSmart += rEntry->balanceEligible;
        }

        rEntry->disqualifyingTx = tx.GetHash();
        rEntry->fDisqualifyingTx = true;

    } else if (!Is_1_3(nCurrentRound) && !rEntry->fDisqualifyingTx) {
        rEntry->disqualifyingTx = tx.GetHash();
        rEntry->fDisqualifyingTx = true;

        if (rEntry->balanceEligible) {
            result.disqualifiedEntries++;
            result.disqualifiedSmart += rEntry->balanceEligible;
        }
    }

    if (rEntry->balance < 0) {
        LogPrint("smartrewards-tx", "CSmartRewards::ProcessInput - Negative amount?! - %s", rEntry->ToString());
        rEntry->balance = 0;
    }
}
/*

    //We can only subtract the balance if is not a vote proof transaction.
    //Vote proof transactions send for itself.
    //Same rule for 1.2 and 1.3
    if (!tx.IsVoteProof()) {
        //Subtract the amount from the entry based in the input.
        rEntry->balance -= in.nValue;
    }
    if (rEntry->balance < 0) {
        LogPrint("smartrewards-tx", "CSmartRewards::ProcessInput - Negative amount?! - %s", rEntry->ToString());
        rEntry->balance = 0;
    }

    if (Is_1_3(nCurrentRound)) {
        if (tx.IsVoteProof()) {
            *voteProofCheck = new CSmartAddress(rEntry->id);
            nVoteProofIn += in.nValue;
            //When it is a vote proof transaction, then we should not disqualify the address
            rEntry->fVoteProven = true;
            rEntry->voteProof == tx.GetHash();
        } else {
            //Let's disqualify the address
            if (!rEntry->fDisqualifyingTx) {
                rEntry->disqualifyingTx = tx.GetHash();
                rEntry->fDisqualifyingTx = true;
            }
            //We mis disqualify the balance as well
            if (rEntry->balance) {
                result.disqualifiedEntries++;
                result.disqualifiedSmart += rEntry->balance;
            }
        }
    } else {
        if (!tx.IsVoteProof()) {
            if (!rEntry->fDisqualifyingTx) {
                rEntry->disqualifyingTx = tx.GetHash();
                rEntry->fDisqualifyingTx = true;
            }
            if (rEntry->balance) {
                result.disqualifiedEntries++;
                result.disqualifiedSmart += rEntry->balance;
            }
        }
    }
}
*/
void CSmartRewards::ProcessOutput(const CTransaction& tx, const CTxOut& out, CSmartAddress* voteProofCheck, CAmount nVoteProofIn, uint16_t nCurrentRound, int nHeight, CSmartRewardsUpdateResult& result)
{
    CSmartRewardEntry* rEntry = nullptr;
    CSmartAddress id;

    if (!ExtractDestination(out.scriptPubKey, id)) {
        LogPrint("smartrewards-tx", "CSmartRewards::ProcessOutput - Could't parse CSmartAddress: %s\n", out.ToString());
        return;
    } else {
        if (GetRewardEntry(id, rEntry, true)) {
            //We only add balance if is not the vote proof transaction. Vote proof transaction is just to activate the address
            if (!tx.IsVoteProof() || !Is_1_3(nCurrentRound)) {
                //                if (!rEntry->fVoteProven || !Is_1_3(nCurrentRound)) {
                if (rEntry->IsEligible()) {
                    result.disqualifiedEntries++;
                    result.disqualifiedSmart += rEntry->balanceEligible;
                }
                rEntry->disqualifyingTx = tx.GetHash();
                rEntry->fDisqualifyingTx = true;
            }
            if (tx.IsVoteProof() && Is_1_3(nCurrentRound)) {
                if (!rEntry->fVoteProven) {
                    rEntry->voteProof = tx.GetHash();
                    rEntry->fVoteProven = true;
                }
                if (rEntry->IsEligible()) {
                    result.qualifiedEntries++;
                    result.qualifiedSmart += rEntry->balanceEligible;
                }
            }
        }
        rEntry->balance += out.nValue;

        if (Is_1_3(nCurrentRound) && tx.IsCoinBase()) {
            int nInterval = SmartNodePayments::PayoutInterval(nHeight);
            int nPayoutsPerBlock = SmartNodePayments::PayoutsPerBlock(nHeight);
            // Just to avoid potential zero divisions
            nPayoutsPerBlock = std::max(1, nPayoutsPerBlock);

            CAmount nNodeReward = SmartNodePayments::Payment(nHeight) / nPayoutsPerBlock;
            // If we have an interval check if this is a node payout block
            if (nInterval && !(nHeight % nInterval)) {
                // If the amount matches and the entry is not yet marked as node do it
                if (abs(out.nValue - nNodeReward) < 2) {
                    if (!rEntry->fSmartnodePaymentTx) {
                        // If it is currently eligible adjust the round's results
                        if (rEntry->IsEligible()) {
                            ++result.disqualifiedEntries;
                            result.disqualifiedSmart += rEntry->balance;
                        }
                        rEntry->smartnodePaymentTx = tx.GetHash();
                        rEntry->fSmartnodePaymentTx = true;
                    }
                }
            }
        }
    }
}

bool CSmartRewards::ProcessTransaction(CBlockIndex* pIndex, const CTransaction& tx, int nCurrentRound)
{
    LogPrint("smartrewards-tx", "CSmartRewards::ProcessTransaction - %s", tx.GetHash().ToString());

    int nHeight = pIndex->nHeight;

    if (nHeight == 0 || nHeight > sporkManager.GetSporkValue(SPORK_15_SMARTREWARDS_BLOCKS_ENABLED)) {
        return false;
    }

    CSmartRewardTransaction testTx;

    // For the first 4 rounds we have zerocoin exploits and we don't want to add them to the rewards db.
    if (nCurrentRound <= 4) {
        // First check if the transaction hash did already come up in the past.
        if (GetTransaction(tx.GetHash(), testTx)) {
            // If yes we want to ignore it! There are some double appearing transactions in the history due to zerocoin exploits.
            LogPrint("smartrewards-tx", "CSmartRewards::ProcessTransaction - [%s] Double appearance! First in %d - Now in %d\n", testTx.hash.ToString(), testTx.blockHeight, pIndex->nHeight);
            return false;
        } else {
            // If not save add it to the cache.
            cache.AddTransaction(CSmartRewardTransaction(pIndex->nHeight, tx.GetHash()));
        }
    }

    return true;
}

void CSmartRewards::UndoInput(const CTransaction& tx, const CTxOut& in, uint16_t nCurrentRound, CSmartRewardsUpdateResult& result)
{
    //    uint32_t nFirst_1_3_Round = Params().GetConsensus().nRewardsFirst_1_3_Round;
    CSmartRewardEntry* rEntry = nullptr;
    CSmartAddress id;

    if (!ExtractDestination(in.scriptPubKey, id)) {
        LogPrint("smartrewards-tx", "CSmartRewards::UndoInput - Process Inputs: Could't parse CSmartAddress: %s\n", in.ToString());
        return;
    }

    if (!GetRewardEntry(id, rEntry, true)) {
        LogPrint("smartrewards-tx", "CSmartRewards::UndoInput - Spend without previous receive - %s", tx.ToString());
        return;
    }

    rEntry->balance += in.nValue;

    if (Is_1_3(nCurrentRound) && rEntry->disqualifyingTx == tx.GetHash()) {
        rEntry->disqualifyingTx.SetNull();
        rEntry->fDisqualifyingTx = false;

        if (rEntry->IsEligible()) {
            --result.disqualifiedEntries;
            result.disqualifiedSmart -= rEntry->balanceEligible;
        }

    } else if (!Is_1_3(nCurrentRound) && rEntry->disqualifyingTx == tx.GetHash()) {
        rEntry->disqualifyingTx.SetNull();
        rEntry->fDisqualifyingTx = false;

        if (rEntry->balanceEligible) {
            --result.disqualifiedEntries;
            result.disqualifiedSmart -= rEntry->balanceEligible;
        }
    }

    if (rEntry->balance < 0) {
        LogPrint("smartrewards-tx", "CSmartRewards::UndoInput - Negative amount?! - %s", rEntry->ToString());
        rEntry->balance = 0;
    }
}

void CSmartRewards::UndoOutput(const CTransaction& tx, const CTxOut& out, CSmartAddress* voteProofCheck, CAmount& nVoteProofIn, uint16_t nCurrentRound, CSmartRewardsUpdateResult& result)
{
    CSmartRewardEntry* rEntry = nullptr;
    CSmartAddress id;

    if (!ExtractDestination(out.scriptPubKey, id)) {
        LogPrint("smartrewards-tx", "CSmartRewards::UndoOutput - Process Outputs: Could't parse CSmartAddress: %s\n", out.ToString());
        return;
    } else {
        GetRewardEntry(id, rEntry, true);
        //        if (Is_1_3(nCurrentRound) && tx.IsCoinBase()) {
        // If it's a voteproof set voteproven and undisqualify transactions
        if (tx.IsVoteProof()) {
            if (!rEntry->fVoteProven) {
                rEntry->voteProof = tx.GetHash();
                rEntry->fVoteProven = true;
            }
            if (rEntry->disqualifyingTx == tx.GetHash()) {
                rEntry->disqualifyingTx.SetNull();
                rEntry->fDisqualifyingTx = false;
                if (rEntry->IsEligible()) {
                    --result.disqualifiedEntries;
                    result.disqualifiedSmart -= rEntry->balanceEligible;
                }
            }
        }
        // If it isn't a voteproof transaction disqualify it.
        if (rEntry->fVoteProven && !SmartHive::IsHive(rEntry->id)) {
            rEntry->voteProof.SetNull();
            rEntry->fVoteProven = false;
            --result.qualifiedEntries;
            result.qualifiedSmart -= rEntry->balanceEligible;
            if (tx.IsVoteProof()) {
                rEntry->balance += nVoteProofIn - tx.GetValueOut();
            }
        }

        rEntry->balance -= out.nValue;

        // If we are in the 1.3 cycles check for node rewards to remove node addresses from lists
        if (Is_1_3(nCurrentRound) && tx.IsCoinBase()) {
            if (rEntry->smartnodePaymentTx == tx.GetHash()) {
                rEntry->smartnodePaymentTx.SetNull();
                rEntry->fSmartnodePaymentTx = false;

                // If it is eligible now adjust the round's results
                if (rEntry->IsEligible()) {
                    --result.disqualifiedEntries;
                    result.disqualifiedSmart -= rEntry->balanceEligible;
                }
            }
        }
    }
}

void CSmartRewards::UndoTransaction(CBlockIndex* pIndex, const CTransaction& tx, CCoinsViewCache& coins, const CChainParams& chainparams, CSmartRewardsUpdateResult& result)
{
    LogPrint("smartrewards-tx", "CSmartRewards::UndoTransaction - %s", tx.GetHash().ToString());

    int nFirst_1_3_Round = chainparams.GetConsensus().nRewardsFirst_1_3_Round;

    int nCurrentRound;

    {
        LOCK(cs_rewardscache);
        nCurrentRound = cache.GetCurrentRound()->number;
    }

    int nHeight = pIndex->nHeight;

    if (nHeight > sporkManager.GetSporkValue(SPORK_15_SMARTREWARDS_BLOCKS_ENABLED)) {
        return;
    }

    int nTime1 = GetTimeMicros();


    CSmartRewardTransaction testTx;

    if (GetTransaction(tx.GetHash(), testTx) && testTx.blockHeight == pIndex->nHeight) {
        cache.RemoveTransaction(testTx);
    } else if (nCurrentRound <= 4) {
        return;
    }

    CSmartAddress* voteProofCheck = nullptr;
    CAmount nVoteProofIn = 0;

    if (Is_1_3(nCurrentRound) && tx.IsVoteProof()) {
        const Coin& coin = coins.AccessCoin(tx.vin[0].prevout);
        const CTxOut& rOut = coin.out;

        CSmartAddress id;

        if (!ExtractDestination(rOut.scriptPubKey, id)) {
            LogPrint("smartrewards-tx", "CSmartRewards::UndoTransaction - Process VoteProof: Could't parse CSmartAddress: %s\n", rOut.ToString());
            return;
        }

        nVoteProofIn = rOut.nValue;
        voteProofCheck = new CSmartAddress(id);
    }

    BOOST_REVERSE_FOREACH (const CTxOut& out, tx.vout) {
        if (out.scriptPubKey.IsZerocoinMint())
            continue;

        UndoOutput(tx, out, voteProofCheck, nVoteProofIn, nCurrentRound, result);
    }

    int nTime2 = GetTimeMicros();

    // No reason to check the input here for new coins.
    if (!tx.IsCoinBase()) {
        BOOST_REVERSE_FOREACH (const CTxIn& in, tx.vin) {
            if (in.scriptSig.IsZerocoinSpend())
                continue;

            const Coin& coin = coins.AccessCoin(in.prevout);
            const CTxOut& rOut = coin.out;

            UndoInput(tx, rOut, nCurrentRound, result);
        }
    }

    int nTime3 = GetTimeMicros();
    double dProcessingTime = (nTime3 - nTime1) * 0.001;

    if (dProcessingTime > 10.0 && LogAcceptCategory("smartrewards-tx")) {
        LogPrint("smartrewards-tx", "CSmartRewards::UndoTransaction - TX %s - %.2fms\n", tx.GetHash().ToString(), dProcessingTime);
        LogPrint("smartrewards-tx", " outputs - %.2fms\n", (nTime2 - nTime1) * 0.001);
        LogPrint("smartrewards-tx", " inputs - %.2fms\n", (nTime3 - nTime2) * 0.001);
    }
}

void CSmartRewards::ExportToCsv()
{
    CSmartRewardEntryMap smartRewardEntriesFromDB;
    pdb->ReadRewardEntries(smartRewardEntriesFromDB);

    std::ofstream myCsv("smartRewardEntriesFromDB.csv");

    myCsv << "key,balance,balanceEligible,balanceAtStart, isDisqualified, isNode, isActive,\n";
    for (auto itDb = smartRewardEntriesFromDB.begin(); itDb != smartRewardEntriesFromDB.end(); ++itDb) {
        auto key = itDb->first.ToString();
        auto str_balance = itDb->second->balance > 0 ? std::to_string(itDb->second->balance / COIN) : "0";
        auto str_balanceEligible = itDb->second->balanceEligible > 0 ? std::to_string(itDb->second->balanceEligible / COIN) : "0";
        auto str_balanceAtStart = itDb->second->balanceAtStart > 0 ? std::to_string(itDb->second->balanceAtStart / COIN) : "0";
        auto str_isDisqualified = itDb->second->fDisqualifyingTx ? "yes" : "no";
        auto str_isNode = itDb->second->fSmartnodePaymentTx ? "yes" : "no";
        auto str_isActive = itDb->second->fVoteProven ? "yes" : "no";

        myCsv << "" + key + "," + str_balance + "," + str_balanceEligible + "," + str_balanceAtStart + ", " + str_isDisqualified + ", " + str_isNode + ", " + str_isActive + ",\n";
    }
    myCsv.close();
}

bool CSmartRewards::CommitBlock(CBlockIndex* pIndex, const CSmartRewardsUpdateResult& result)
{
    int nTime1 = GetTimeMicros();

    if (pIndex && pIndex->nHeight > sporkManager.GetSporkValue(SPORK_15_SMARTREWARDS_BLOCKS_ENABLED)) {
        return true;
    }

    LOCK(cs_rewardscache);

    const CSmartRewardRound* round = cache.GetCurrentRound();

    if (!pIndex || pIndex->nHeight != cache.GetCurrentBlock()->nHeight + 1) {
        LogPrintf("CSmartRewards::CommitBlock - Invalid nextRewardRound block!");
        return false;
    }

    if (cache.GetLastRoundResult() &&
        cache.GetLastRoundResult()->fSynced &&
        pIndex->nHeight > cache.GetLastRoundResult()->round.GetLastRoundBlock()) {
        cache.ClearResult();
    }

    cache.ApplyRoundUpdateResult(result);

    ExportToCsv();

    // For the first round we have special parameter..
    if (!round->number) {
        if ((MainNet() && pIndex->GetBlockTime() > nFirstRoundStartTime) ||
            (TestNet() && pIndex->nHeight >= nFirstRoundStartBlock_Testnet)) {
            // Create the very first smartrewards round.
            CSmartRewardRound first;
            first.number = 1;
            first.startBlockTime = pIndex->GetBlockTime();
            first.startBlockHeight = MainNet() ? nFirstRoundStartBlock : nFirstRoundStartBlock_Testnet;
            first.endBlockTime = MainNet() ? nFirstRoundEndTime : nFirstRoundEndTime_Testnet;
            // Estimate the block, gets updated on the end of the round to the real one.
            first.endBlockHeight = MainNet() ? nFirstRoundEndBlock : nFirstRoundEndBlock_Testnet;

            // Evaluate the round and update the nextRewardRound rounds parameter.
            EvaluateRound(first);
        }
    }

    // If just hit the nextRewardRound round threshold
    if ((MainNet() && round->number < nRewardsFirstAutomatedRound - 1 && pIndex->GetBlockTime() > round->endBlockTime) ||
        ((TestNet() || round->number >= nRewardsFirstAutomatedRound - 1) && pIndex->nHeight == round->endBlockHeight)) {
        UpdatePercentage();

        cache.UpdateRoundEnd(pIndex->nHeight, pIndex->GetBlockTime());

        // Create the nextRewardRound round.
        CSmartRewardRound nextRewardRound;
        nextRewardRound.number = round->number + 1;
        nextRewardRound.startBlockTime = round->endBlockTime;
        nextRewardRound.startBlockHeight = round->endBlockHeight + 1;

        int nBlocksPerRound = GetBlocksPerRound(nextRewardRound.number);
        time_t startTime = (time_t)nextRewardRound.startBlockTime;

        if (MainNet()) {
            if (nextRewardRound.number == nRewardsFirstAutomatedRound - 1) {
                // Let the round 12 end at height 574099 so that round 13 starts at 574100
                nextRewardRound.endBlockHeight = HF_V1_2_SMARTREWARD_HEIGHT - 1;
                nextRewardRound.endBlockTime = startTime + ((nextRewardRound.endBlockHeight - nextRewardRound.startBlockHeight) * 55);
            } else if (nextRewardRound.number < nRewardsFirstAutomatedRound) {
                boost::gregorian::date endDate = boost::posix_time::from_time_t(startTime).date();

                endDate += boost::gregorian::months(1);
                // End date at 00:00:00 + 25200 seconds (7 hours) to match the date at 07:00 UTC
                nextRewardRound.endBlockTime = time_t((boost::posix_time::ptime(endDate, boost::posix_time::seconds(25200)) - epoch).total_seconds());
                nextRewardRound.endBlockHeight = nextRewardRound.startBlockHeight + ((nextRewardRound.endBlockTime - nextRewardRound.startBlockTime) / 55);
            } else {
                nextRewardRound.endBlockHeight = nextRewardRound.startBlockHeight + nBlocksPerRound - 1;
                nextRewardRound.endBlockTime = startTime + nBlocksPerRound * 55;
            }

        } else {
            nextRewardRound.endBlockHeight = nextRewardRound.startBlockHeight + nBlocksPerRound - 1;
            nextRewardRound.endBlockTime = startTime + nBlocksPerRound * 55;
        }

        // Evaluate the round and update the nextRewardRound rounds parameter.
        EvaluateRound(nextRewardRound);
    }

    UpdatePercentage();

    cache.UpdateHeights(GetBlockHeight(pIndex), cache.GetCurrentBlock()->nHeight);

    if (LogAcceptCategory("smartrewards-bench")) {
        int nTime2 = GetTimeMicros();
        double dProcessingTime = (nTime2 - nTime1) * 0.001;
        LogPrint("smartrewards-bench", "Round %d - Block: %d\n", round->number, cache.GetCurrentBlock()->nHeight);
        LogPrint("smartrewards-bench", "  Commit block: %.2fms\n", dProcessingTime);
    }

    // If we are synced notify the UI on each new block.
    // If not notify the UI every nRewardsUISyncUpdateRate blocks to let it update the
    // loading screen.
    if (IsSynced() || !(cache.GetCurrentBlock()->nHeight % nRewardsUISyncUpdateRate))
        uiInterface.NotifySmartRewardUpdate();

    return true;
}

bool CSmartRewards::CommitUndoBlock(CBlockIndex* pIndex, const CSmartRewardsUpdateResult& result)
{
    int nTime1 = GetTimeMicros();

    if (pIndex && pIndex->nHeight > sporkManager.GetSporkValue(SPORK_15_SMARTREWARDS_BLOCKS_ENABLED)) {
        return true;
    }

    LOCK2(cs_rewardscache, cs_rewardsdb);

    if (!pIndex || pIndex->nHeight != cache.GetCurrentBlock()->nHeight) {
        LogPrintf("CSmartRewards::CommitUndoBlock - Invalid nextRewardRound block!");
        return false;
    }

    const CSmartRewardRound* pRound = cache.GetCurrentRound();

    CSmartRewardsUpdateResult undoUpdate(pIndex->pprev->nHeight, pIndex->pprev->phashBlock, pIndex->pprev->GetBlockTime());

    undoUpdate.disqualifiedEntries = result.disqualifiedEntries;
    undoUpdate.disqualifiedSmart = result.disqualifiedSmart;
    undoUpdate.qualifiedEntries = result.qualifiedEntries;
    undoUpdate.qualifiedSmart = result.qualifiedSmart;

    cache.ApplyRoundUpdateResult(undoUpdate);

    // If just hit the last round's threshold
    if ((MainNet() && pRound->number < nRewardsFirstAutomatedRound - 1 && pIndex->GetBlockTime() < pRound->endBlockTime) ||
        (((TestNet() && pRound->number > 1) || pRound->number >= nRewardsFirstAutomatedRound - 1) && pIndex->nHeight == pRound->startBlockHeight)) {
        // Recover the last round from the history as current round
        CSmartRewardRound prevRound = cache.GetRounds()->at(pRound->number - 1);

        cache.SetCurrentRound(prevRound);
        cache.RemoveFinishedRound(prevRound.number);

        CSmartRewardsRoundResult* undoResult = new CSmartRewardsRoundResult();

        undoResult->round = prevRound;

        if (!GetRewardRoundResults(prevRound.number, undoResult->results)) {
            LogPrintf("CSmartRewards::CommitUndoBlock - Failed to read last round's results!");
            return false;
        }

        cache.SetUndoResult(undoResult);

        // Load all entries into the cache
        CSmartRewardEntryMap smartRewardEntriesFromDB;
        pdb->ReadRewardEntries(smartRewardEntriesFromDB);

        for (auto itDb = smartRewardEntriesFromDB.begin(); itDb != smartRewardEntriesFromDB.end(); ++itDb) {
            auto itCache = cache.GetEntries()->find(itDb->first);

            if (itCache == cache.GetEntries()->end()) {
                cache.AddEntry(itDb->second);
            } else {
                delete itDb->second;
            }
        }
    }

    UpdatePercentage();

    cache.UpdateHeights(GetBlockHeight(pIndex), cache.GetCurrentBlock()->nHeight);

    int nTime2 = GetTimeMicros();

    if (LogAcceptCategory("smartrewards-block")) {
        LogPrint("smartrewards-block", "Round %d - Block: %d\n", pRound->number, cache.GetCurrentBlock()->nHeight);
        LogPrint("smartrewards-block", "  Commit undo block: %.2fms\n", (nTime2 - nTime1) * 0.001);
    }

    return true;
}

CSmartRewardsCache::~CSmartRewardsCache()
{
    LOCK(cs_rewardscache);

    for (std::pair<CSmartAddress, CSmartRewardEntry*> it : entries) {
        delete it.second;
    }

    if (result) {
        result->Clear();
        delete result;
    }

    block = CSmartRewardBlock();
    round = CSmartRewardRound();
    rounds.clear();
    addTransactions.clear();
    removeTransactions.clear();
    entries.clear();
}

unsigned long CSmartRewardsCache::EstimatedSize()
{
    unsigned long nEntriesSize = entries.size() * (sizeof(CSmartAddress) + sizeof(CSmartRewardEntry));
    unsigned long nRoundsSize = (rounds.size() + 1) * sizeof(CSmartRewardRound);
    unsigned long nTransactionsSize = (addTransactions.size() + removeTransactions.size()) * (sizeof(uint256) + sizeof(CSmartRewardTransaction));
    unsigned long nBlockSize = sizeof(CSmartRewardBlock);
    return nEntriesSize + nRoundsSize + nTransactionsSize + nBlockSize;
}

void CSmartRewardsCache::Load(const CSmartRewardBlock& block, const CSmartRewardRound& round, const CSmartRewardRoundMap& rounds)
{
    this->block = block;
    this->round = round;
    this->rounds = rounds;
}

bool CSmartRewardsCache::NeedsSync()
{
    LOCK(cs_rewardscache);
    return (result != nullptr && !result->fSynced) ||
           (undoResults != nullptr && !undoResults->fSynced) ||
           EstimatedSize() > REWARDS_MAX_CACHE || entries.size() > nCacheRewardEntries;
}

void CSmartRewardsCache::Clear()
{
    LOCK(cs_rewardscache);

    if (result) {
        result->fSynced = true;
    }

    if (undoResults) {
        undoResults->fSynced = true;
    }

    for (auto it = entries.cbegin(); it != entries.cend(); it++) {
        delete it->second;
    }

    entries.clear();
    addTransactions.clear();
    removeTransactions.clear();
}

void CSmartRewardsCache::ClearResult()
{
    if (result) {
        auto it = result->results.begin();

        while (it != result->results.end()) {
            delete *it;
            ++it;
        }

        result->results.clear();
        result->payouts.clear();
        delete result;
        result = nullptr;
    }

    if (undoResults) {
        auto it = undoResults->results.begin();

        while (it != undoResults->results.end()) {
            delete *it;
            ++it;
        }

        undoResults->results.clear();
        undoResults->payouts.clear();
        delete undoResults;
        undoResults = nullptr;
    }
}

void CSmartRewardsCache::SetCurrentBlock(const CSmartRewardBlock& currentBlock)
{
    AssertLockHeld(cs_rewardscache);
    block = currentBlock;
}

void CSmartRewardsCache::SetCurrentRound(const CSmartRewardRound& currentRound)
{
    AssertLockHeld(cs_rewardscache);
    round = currentRound;
}

void CSmartRewardsCache::SetResult(CSmartRewardsRoundResult* pResult)
{
    AssertLockHeld(cs_rewardscache);

    if (result) {
        result->Clear();
        delete result;
    }

    result = pResult;
}

void CSmartRewardsCache::SetUndoResult(CSmartRewardsRoundResult* pResult)
{
    AssertLockHeld(cs_rewardscache);

    if (undoResults) {
        undoResults->Clear();
        delete undoResults;
    }

    undoResults = pResult;
}

void CSmartRewardsCache::ApplyRoundUpdateResult(const CSmartRewardsUpdateResult& result)
{
    AssertLockHeld(cs_rewardscache);

    if (result.IsValid()) {
        SetCurrentBlock(result.block);

        round.disqualifiedEntries += result.disqualifiedEntries;
        round.disqualifiedSmart += result.disqualifiedSmart;

        round.eligibleEntries += result.qualifiedEntries;
        round.eligibleSmart += result.qualifiedSmart;
    }
}

void CSmartRewardsCache::UpdateRoundPayoutParameter(int64_t nBlockPayees, int64_t nBlockInterval)
{
    AssertLockHeld(cs_rewardscache);

    round.nBlockPayees = nBlockPayees;
    round.nBlockInterval = nBlockInterval;
}

void CSmartRewardsCache::UpdateRoundEnd(int nBlockHeight, int64_t nBlockTime)
{
    AssertLockHeld(cs_rewardscache);

    round.endBlockHeight = nBlockHeight;
    round.endBlockTime = nBlockTime;
}

void CSmartRewardsCache::UpdateRoundPercent(double dPercent)
{
    AssertLockHeld(cs_rewardscache);

    round.percent = dPercent;
}

void CSmartRewardsCache::UpdateHeights(const int nHeight, const int nRewardHeight)
{
    AssertLockHeld(cs_rewardscache);

    chainHeight = nHeight;
    rewardHeight = nRewardHeight;
}

void CSmartRewardsCache::AddFinishedRound(const CSmartRewardRound& round)
{
    AssertLockHeld(cs_rewardscache);

    rounds[round.number] = round;
}

void CSmartRewardsCache::RemoveFinishedRound(const int& nNumber)
{
    AssertLockHeld(cs_rewardscache);

    auto it = rounds.find(nNumber);

    if (it != rounds.end()) {
        rounds.erase(it);
    }
}

void CSmartRewardsCache::AddTransaction(const CSmartRewardTransaction& transaction)
{
    LOCK(cs_rewardscache);
    auto it = removeTransactions.find(transaction.hash);

    if (it != removeTransactions.end()) {
        removeTransactions.erase(it);
    } else {
        addTransactions.insert(std::make_pair(transaction.hash, transaction));
    }
}

void CSmartRewardsCache::RemoveTransaction(const CSmartRewardTransaction& transaction)
{
    LOCK(cs_rewardscache);
    auto it = addTransactions.find(transaction.hash);

    if (it != addTransactions.end()) {
        addTransactions.erase(it);
    } else {
        removeTransactions.insert(std::make_pair(transaction.hash, transaction));
    }
}

void CSmartRewardsCache::AddEntry(CSmartRewardEntry* entry)
{
    LOCK(cs_rewardscache);
    entries[entry->id] = entry;
}

void CSmartRewardsRoundResult::Clear()
{
    for (CSmartRewardResultEntry* resultEntry : results) {
        delete resultEntry;
    }

    results.clear();
    payouts.clear();
}
