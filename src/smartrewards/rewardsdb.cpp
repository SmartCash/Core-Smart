
#include "smartrewards/rewardsdb.h"

#include "chainparams.h"
#include "hash.h"
#include "pow.h"
#include "uint256.h"
#include "ui_interface.h"
#include "init.h"

#include <stdint.h>

#include <boost/thread.hpp>

using namespace std;

static const char DB_ROUND_CURRENT = 'R';
static const char DB_ROUND = 'r';
static const char DB_ROUND_PAID = 'p';

static const char DB_REWARD_ENTRY = 'E';
static const char DB_BLOCK = 'B';
static const char DB_BLOCK_LAST = 'b';

static const char DB_VERSION = 'V';
static const char DB_REINDEX = 'f';


CSmartRewardsDB::CSmartRewardsDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "rewards", nCacheSize, fMemory, fWipe) {

    CSmartRewardsBlock block;
    if(ReadLastBlock(block))
        LogPrintf("CSmartRewardsDB(last block = %s", block.ToString());
    else
        LogPrintf("CSmartRewardsDB(no block available)");

}

bool CSmartRewardsDB::Verify()
{
    int nHeight = -1;
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
    CSmartRewardsBlock last;

    if(!ReadLastBlock(last)){
        LogPrintf("CSmartRewards::Verify() No block here yet\n");
        return true;
    }

    LogPrintf("CSmartRewards::Verify() Verify blocks 0 - %d\n", last.nHeight);

    std::vector<CSmartRewardsBlock> testBlocks;

    pcursor->Seek(make_pair(DB_BLOCK,0));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,int> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK) {
            CSmartRewardsBlock nValue;
            if (pcursor->GetValue(nValue)) {
                if( nValue.nHeight != key.second ) return error("Block value %d contains wrong height: %s",key.second, nValue.ToString());
                pcursor->Next();
                testBlocks.push_back(nValue);
            } else {
                return error("failed to get block entry %d", key.second);
            }
        } else {
            if( testBlocks.size() < size_t(last.nHeight) ) return error("Odd block count %d <> %d", nHeight, last.nHeight);
            break;
        }
    }

    std::sort(testBlocks.begin(), testBlocks.end());
    vector<CSmartRewardsBlock>::iterator it;
    for(it = testBlocks.begin() + 1; it != testBlocks.end(); it++ )    {
        if( (it-1)->nHeight + 1 != it->nHeight) return error("Block %d missing", it->nHeight);
    }

    return true;
}

bool CSmartRewardsDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX, '1');
    else
        return Erase(DB_REINDEX);
}

bool CSmartRewardsDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX);
    return true;
}

bool CSmartRewardsDB::ReadBlock(const int nHeight, CSmartRewardsBlock &block)
{
    return Read(make_pair(DB_BLOCK,nHeight), block);
}

bool CSmartRewardsDB::ReadLastBlock(CSmartRewardsBlock &block)
{
    return Read(DB_BLOCK_LAST, block);
}

bool CSmartRewardsDB::ReadRound(const int number, CSmartRewardsRound &round)
{
    return Read(make_pair(DB_ROUND,number), round);
}

bool CSmartRewardsDB::WriteRound(const CSmartRewardsRound &round)
{
    return Write(make_pair(DB_ROUND,round.number), round);
}

bool CSmartRewardsDB::ReadRewardRounds(std::vector<CSmartRewardsRound> &vect)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(DB_ROUND);

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,int16_t> key;
        if (pcursor->GetKey(key) && key.first == DB_ROUND) {
            CSmartRewardsRound nValue;
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

bool CSmartRewardsDB::ReadCurrentRound(CSmartRewardsRound &round)
{
    return Read(DB_ROUND_CURRENT, round);
}

bool CSmartRewardsDB::WriteCurrentRound(const CSmartRewardsRound &round)
{
    return Write(DB_ROUND_CURRENT, round);
}

bool CSmartRewardsDB::ReadRewardEntry(const CScript &pubKey, CSmartRewardEntry &entry)
{
    return Read(make_pair(DB_REWARD_ENTRY,*(CScriptBase*)(&pubKey)), entry);
}

bool CSmartRewardsDB::WriteRewardEntry(const CSmartRewardEntry &entry)
{
    return Write(make_pair(DB_REWARD_ENTRY, *(CScriptBase*)(&entry.pubKey)), entry);
}

bool CSmartRewardsDB::Sync(const std::vector<CSmartRewardsBlock> &blocks, const std::vector<CSmartRewardEntry> &update, const std::vector<CSmartRewardEntry> &remove)
{

    CDBBatch batch(*this);

    BOOST_FOREACH(CSmartRewardEntry e, remove) {
        batch.Erase(make_pair(DB_REWARD_ENTRY,*(CScriptBase*)(&e.pubKey)));
        //LogPrintf("Erase reward entry %s", e.ToString());
    }
    BOOST_FOREACH(CSmartRewardEntry e, update) {
        batch.Write(make_pair(DB_REWARD_ENTRY,*(CScriptBase*)(&e.pubKey)), e);
        LogPrintf("Update reward entry %s", e.ToString());
    }

    CSmartRewardsBlock last;

    BOOST_FOREACH(CSmartRewardsBlock b, blocks) {
        batch.Write(make_pair(DB_BLOCK,b.nHeight), b);

        if(b.nHeight > last.nHeight) last = b;
    }

    batch.Write(DB_BLOCK_LAST, last);

    return WriteBatch(batch, true);
}

bool CSmartRewardsDB::ReadRewardEntries(std::vector<CSmartRewardEntry> &entries) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(DB_REWARD_ENTRY);

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CScriptBase> key;
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

bool CSmartRewardsDB::WriteRewardEntries(const std::vector<CSmartRewardEntry>&vect) {
    CDBBatch batch(*this);
    for (std::vector<CSmartRewardEntry>::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_REWARD_ENTRY, *(CScriptBase*)(&it->pubKey)), *it);
    return WriteBatch(batch, true);
}


bool CSmartRewardsDB::ReadRewardPayouts(const int64_t round, std::vector<CSmartRewardPayout> &vect) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ROUND_PAID,round));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,std::pair<int64_t, CScriptBase>> key;
        if (pcursor->GetKey(key) && key.first == DB_ROUND_PAID) {
            CSmartRewardPayout nValue;
            if (pcursor->GetValue(nValue)) {
                vect.push_back(nValue);
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

bool CSmartRewardsDB::WriteRewardPayouts(const int64_t round, const std::vector<CSmartRewardPayout>&vect) {
    CDBBatch batch(*this);
    for (std::vector<CSmartRewardPayout>::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_ROUND_PAID, make_pair(round, *(CScriptBase*)(&it->pubKey))), *it);
    return WriteBatch(batch, true);
}

string CSmartRewardEntry::GetAddress() const
{

}

void CSmartRewardEntry::setNull()
{
    pubKey.clear();
    balanceLastStart = 0;
    balanceOnStart = 0;
    balance = 0;
    eligible = false;
}

string CSmartRewardEntry::ToString() const
{
    std::stringstream s;
    s << strprintf("CSmartRewardEntry(pubKey=%s, balance=%d, balanceStart=%d, balanceLastStart=%d, eligible=%b)\n",
        HexStr(pubKey.begin(), pubKey.end()),
        balance,
        balanceOnStart,
        balanceLastStart,
        eligible);
    return s.str();
}

string CSmartRewardsBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CSmartRewardsBlock(height=%d, hash=%s, time=%d)\n",
        nHeight,
        blockHash.ToString(),
        blockTime);
    return s.str();
}

string CSmartRewardsRound::ToString() const
{
    std::stringstream s;
    s << strprintf("CSmartRewardsRound(number=%d, start(block)=%d, start(time)=%d, end(block)=%d, end(time)=%d\n"
                   "  Eligible entries=%d\n  Eligible amount=%d\n  Percent=%f)\n",
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

string CSmartRewardPayout::GetAddress() const
{

}

string CSmartRewardPayout::ToString() const
{
    std::stringstream s;
    s << strprintf("CSmartRewardPayout(pubKey=%d, balance=%d, reward=%d\n",
        HexStr(pubKey.begin(), pubKey.end()),
        balance,
        reward);
    return s.str();
}
