// Copyright (c) 2018 dustinface - SmartCash Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartrewards/rewardsdb.h"

#include "chainparams.h"
#include "hash.h"
#include "pow.h"
#include "uint256.h"
#include "ui_interface.h"
#include "init.h"
#include "rewardsdb.h"

#include <stdint.h>

#include <boost/thread.hpp>
#include "leveldb/include/leveldb/db.h"

using namespace std;

static const char DB_ROUND_CURRENT = 'R';
static const char DB_ROUND = 'r';
static const char DB_ROUND_SNAPSHOT = 's';

static const char DB_REWARD_ENTRY = 'E';
static const char DB_BLOCK = 'B';
static const char DB_BLOCK_LAST = 'b';
static const char DB_TX_HASH = 't';

static const char DB_VERSION = 'V';
static const char DB_LOCK = 'L';

CSmartRewardsDB::CSmartRewardsDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "rewards", nCacheSize, fMemory, fWipe) {

    locked = false;

    if( !Exists(DB_VERSION) ){
        Write(DB_VERSION, REWARDS_DB_VERSION);
    }

}

bool CSmartRewardsDB::Verify(int& lastBlockHeight)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
    CSmartRewardBlock last;
    uint8_t dbVersion;

    lastBlockHeight = 0;

    if( !Read(DB_VERSION, dbVersion) ){
        LogPrintf("CSmartRewards::Verify() Could't read DB_VERSION\n");
        return false;
    }

    if( dbVersion < REWARDS_DB_VERSION ){
        LogPrintf("CSmartRewards::Verify() DB_VERSION too old.\n");
        return false;
    }

    if(!ReadLastBlock(last)){
        LogPrintf("CSmartRewards::Verify() No block here yet\n");
        return true;
    }

    lastBlockHeight = last.nHeight;

    LogPrintf("CSmartRewards::Verify() Verify blocks 1 - %d\n", last.nHeight);

    std::vector<CSmartRewardBlock> testBlocks;

    pcursor->Seek(DB_BLOCK);

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,int> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK) {
            CSmartRewardBlock nValue;
            if (pcursor->GetValue(nValue)) {
                if( nValue.nHeight != key.second ) return error("Block value %d contains wrong height: %s",key.second, nValue.ToString());
                pcursor->Next();
                testBlocks.push_back(nValue);
            } else {
                return error("failed to get block entry %d", key.second);
            }
        } else {
            if( testBlocks.size() < size_t(last.nHeight) ) return error("Odd block count %d <> %d", testBlocks.size(), last.nHeight);
            break;
        }
    }

    std::sort(testBlocks.begin(), testBlocks.end());
    vector<CSmartRewardBlock>::iterator it;
    for(it = testBlocks.begin() + 1; it != testBlocks.end(); it++ )    {
        if( (it-1)->nHeight + 1 != it->nHeight) return error("Block %d missing", it->nHeight);
    }

    return true;
}

void CSmartRewardsDB::Lock()
{
    locked = true;
    Write(DB_LOCK, 1, true);
    Sync();
}

void CSmartRewardsDB::Unlock()
{
    if(locked){
        Erase(DB_LOCK,true);
        Sync();
    }
}

bool CSmartRewardsDB::IsLocked()
{
    return Exists(DB_LOCK);
}

// --- TBD ---
bool CSmartRewardsDB::ResetToRound(const int16_t number, const CSmartRewardRound &round, const CSmartRewardEntryList &entries)
{
//    CDBBatch batch(*this);

//    CSmartRewardRoundList rounds;
//    if( !ReadRounds(rounds) ) return false;

//    BOOST_FOREACH(CSmartRewardRound &r, rounds)
//    {
//        if( r.number < number) continue;

//        batch.Erase(make_pair(DB_ROUND, r.number));

//        boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

//        pcursor->Seek(make_pair(DB_ROUND_SNAPSHOT,number));

//        while (pcursor->Valid()) {
//            std::pair<char,std::pair<int16_t, CSmartAddress>> key;
//            if (pcursor->GetKey(key) && key.first == DB_ROUND_SNAPSHOT) {

//                if( key.second.first != round ) break;

//                batch.Erase(key);

//                pcursor->Next();
//            } else {
//                break;
//            }
//        }
//    }

//    BOOST_FOREACH(CSmartRewardEntry e, entries) {
//        batch.Write(make_pair(DB_REWARD_ENTRY,e.id), e);
//    }

//    batch.Write(make_pair(DB_ROUND,round.number), round);
//    batch.Write(DB_ROUND_CURRENT, round);
    return false;
}

bool CSmartRewardsDB::ReadBlock(const int nHeight, CSmartRewardBlock &block)
{
    return Read(make_pair(DB_BLOCK,nHeight), block);
}

bool CSmartRewardsDB::ReadLastBlock(CSmartRewardBlock &block)
{
    return Read(DB_BLOCK_LAST, block);
}

bool CSmartRewardsDB::ReadTransaction(const uint256 hash, CSmartRewardTransaction &transaction)
{
    return Read(make_pair(DB_TX_HASH,hash), transaction);
}

bool CSmartRewardsDB::ReadRound(const int16_t number, CSmartRewardRound &round)
{
    return Read(make_pair(DB_ROUND,number), round);
}

bool CSmartRewardsDB::ReadRounds(CSmartRewardRoundList &vect)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(DB_ROUND);

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,int16_t> key;
        if (pcursor->GetKey(key) && key.first == DB_ROUND) {
            CSmartRewardRound nValue;
            if (pcursor->GetValue(nValue)) {
                vect.push_back(nValue);
                pcursor->Next();
            } else {
                return error("failed to get reward round");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CSmartRewardsDB::ReadCurrentRound(CSmartRewardRound &round)
{
    return Read(DB_ROUND_CURRENT, round);
}

bool CSmartRewardsDB::ReadRewardEntry(const CSmartAddress &id, CSmartRewardEntry &entry)
{
    return Read(make_pair(DB_REWARD_ENTRY,id), entry);
}

bool CSmartRewardsDB::SyncBlocks(const CSmartRewardBlockList &blocks, const CSmartRewardRound& current, const CSmartRewardEntryMap &rewards, const CSmartRewardTransactionList &transactions)
{

    CDBBatch batch(*this);

    if(!blocks.size()) return true;

    BOOST_FOREACH(const PAIRTYPE(CSmartAddress, CSmartRewardEntry*)& r, rewards) {
        if( r.second->balance <= 0 ){
            batch.Erase(make_pair(DB_REWARD_ENTRY,r.first));
        }else{
            batch.Write(make_pair(DB_REWARD_ENTRY,r.first), *r.second);
        }
        delete r.second;
    }

    BOOST_FOREACH(const CSmartRewardTransaction &t, transactions) {
        batch.Write(make_pair(DB_TX_HASH,t.hash), t);
    }

    BOOST_FOREACH(const CSmartRewardBlock &b, blocks) {
        batch.Write(make_pair(DB_BLOCK,b.nHeight), b);
    }

    batch.Write(DB_ROUND_CURRENT, current);

    auto last = std::max_element(blocks.begin(), blocks.end());

    batch.Write(DB_BLOCK_LAST, *last);

    return WriteBatch(batch, true);
}

bool CSmartRewardsDB::StartFirstRound(const CSmartRewardRound &start, const CSmartRewardEntryList &entries)
{
    CDBBatch batch(*this);

    BOOST_FOREACH(const CSmartRewardEntry &e, entries) {
        batch.Write(make_pair(DB_REWARD_ENTRY,e.id), e);
    }

    batch.Write(DB_ROUND_CURRENT, start);

    return WriteBatch(batch, true);
}

bool CSmartRewardsDB::FinalizeRound(const CSmartRewardRound &current, const CSmartRewardRound &next, const CSmartRewardEntryList &entries, const CSmartRewardSnapshotList &snapshots)
{
    CDBBatch batch(*this);

    BOOST_FOREACH(const CSmartRewardSnapshot &s, snapshots) {
        batch.Write(make_pair(DB_ROUND_SNAPSHOT, make_pair(current.number, s.id)), s);
    }

    BOOST_FOREACH(const CSmartRewardEntry &e, entries) {
        batch.Write(make_pair(DB_REWARD_ENTRY,e.id), e);
    }

    batch.Write(make_pair(DB_ROUND,current.number), current);
    batch.Write(DB_ROUND_CURRENT, next);

    return WriteBatch(batch, true);
}

bool CSmartRewardsDB::ReadRewardEntries(CSmartRewardEntryList &entries) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(DB_REWARD_ENTRY);

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CSmartAddress> key;
        if (pcursor->GetKey(key) && key.first == DB_REWARD_ENTRY) {
            CSmartRewardEntry nValue;
            if (pcursor->GetValue(nValue)) {
                entries.push_back(nValue);
                pcursor->Next();
            } else {
                return error("failed to get reward entry");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CSmartRewardsDB::ReadRewardSnapshots(const int16_t round, CSmartRewardSnapshotList &snapshots) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ROUND_SNAPSHOT,round));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,std::pair<int16_t, CSmartAddress>> key;
        if (pcursor->GetKey(key) && key.first == DB_ROUND_SNAPSHOT) {

            if( key.second.first != round ) break;

            CSmartRewardSnapshot nValue;
            if (pcursor->GetValue(nValue)) {
                snapshots.push_back(nValue);
                pcursor->Next();
            } else {
                return error("failed to get reward entry");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CSmartRewardsDB::ReadRewardPayouts(const int16_t round, CSmartRewardSnapshotList &payouts) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ROUND_SNAPSHOT,round));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,std::pair<int16_t, CSmartAddress>> key;
        if (pcursor->GetKey(key) && key.first == DB_ROUND_SNAPSHOT) {

            if( key.second.first != round ) break;

            CSmartRewardSnapshot nValue;
            if (pcursor->GetValue(nValue)) {
                if( nValue.reward ) payouts.push_back(nValue);
                pcursor->Next();
            } else {
                return error("failed to get reward entry");
            }
        } else {
            break;
        }
    }

    return true;
}

string CSmartRewardEntry::GetAddress() const
{
    return id.ToString();
}

void CSmartRewardEntry::setNull()
{
    id = CSmartAddress();
    balanceOnStart = 0;
    balance = 0;
    eligible = false;
}

string CSmartRewardEntry::ToString() const
{
    std::stringstream s;
    s << strprintf("CSmartRewardEntry(id=%s, balance=%d, balanceStart=%d, eligible=%b)\n",
        GetAddress(),
        balance,
        balanceOnStart,
        eligible);
    return s.str();
}

string CSmartRewardBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CSmartRewardBlock(height=%d, hash=%s, time=%d)\n",
        nHeight,
        blockHash.ToString(),
        blockTime);
    return s.str();
}

string CSmartRewardRound::ToString() const
{
    std::stringstream s;
    s << strprintf("CSmartRewardRound(number=%d, start(block)=%d, start(time)=%d, end(block)=%d, end(time)=%d\n"
                   "  Eligible addresses=%d\n  Eligible SMART=%d\n Percent=%f)\n",
        number,
        startBlockHeight,
        startBlockTime,
        endBlockHeight,
        endBlockTime,
        eligibleEntries,
        eligibleSmart,
        percent);
    return s.str();
}

string CSmartRewardSnapshot::GetAddress() const
{
    return id.ToString();
}

string CSmartRewardSnapshot::ToString() const
{
    std::stringstream s;
    s << strprintf("CSmartRewardSnapshot(id=%d, balance=%d, reward=%d\n",
        GetAddress(),
        balance,
        reward);
    return s.str();
}

string CSmartRewardTransaction::ToString() const
{
    std::stringstream s;
    s << strprintf("CSmartRewardTransaction(hash=%s, blockHeight=%d\n",
        hash.ToString(),
        blockHeight);
    return s.str();
}
