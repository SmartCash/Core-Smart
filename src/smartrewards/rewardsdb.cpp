
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

static const char DB_CURRENT_ROUND = 'R';
static const char DB_ROUND = 'r';
static const char DB_ROUND_PAID = 'p';

static const char DB_REWARD_ENTRY = 'E';
static const char DB_BLOCK_FLAG = 'B';
static const char DB_BLOCK_LAST = 'b';

static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'f';


CSmartRewardsDB::CSmartRewardsDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "rewards", nCacheSize, fMemory, fWipe) {

    CSmartRewardsBlock block;
    if(ReadLastBlock(block))
        LogPrintf("CSmartRewardsDB(last block = %s", block.ToString());
    else
        LogPrintf("CSmartRewardsDB(no block available)");

}

bool CSmartRewardsDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CSmartRewardsDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CSmartRewardsDB::ReadLastBlock(CSmartRewardsBlock &block)
{
    return Read(DB_BLOCK_LAST, block);
}

bool CSmartRewardsDB::ReadRewardEntry(const CScript &pubKey, CSmartRewardEntry &entry)
{
    return Read(make_pair(DB_REWARD_ENTRY,*(CScriptBase*)(&pubKey)), entry);
}

bool CSmartRewardsDB::WriteRewardEntry(const CSmartRewardEntry &entry)
{
    return Write(make_pair(DB_REWARD_ENTRY, *(CScriptBase*)(&entry.pubKey)), entry);
}

bool CSmartRewardsDB::SyncBlock(const CSmartRewardsBlock &block, const std::vector<CSmartRewardEntry> &update, const std::vector<CSmartRewardEntry> &remove)
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

    batch.Write(DB_BLOCK_LAST, block);
    batch.Write(make_pair(DB_BLOCK_FLAG,block.nHeight), block);

    return WriteBatch(batch, true);
}

//bool CSmartRewardsDB::ReadRewardsRound(uint16_t &round)
//{
//    uint16_t roundRead;
//    if (!Read(DB_ROUND, roundRead))
//        return false;
//    fValue = roundRead;
//    return true;
//}

//bool CSmartRewardsDB::WriteRewardsRound(const uint16_t round)
//{
//    return Write(DB_ROUND, round);
//}



//bool CSmartRewardsDB::LoadBlockIndexGuts(boost::function<CBlockIndex*(const uint256&)> insertBlockIndex)
//{
//    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

//    pcursor->Seek(make_pair(DB_BLOCK_INDEX, uint256()));

//    // Load mapBlockIndex
//    while (pcursor->Valid()) {
//        boost::this_thread::interruption_point();
//        std::pair<char, uint256> key;
//        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
//            CDiskBlockIndex diskindex;
//            if (pcursor->GetValue(diskindex)) {
//                // Construct block index object
//                CBlockIndex* pindexNew = insertBlockIndex(diskindex.GetBlockHash());
//                pindexNew->pprev          = insertBlockIndex(diskindex.hashPrev);
//                pindexNew->nHeight        = diskindex.nHeight;
//                pindexNew->nFile          = diskindex.nFile;
//                pindexNew->nDataPos       = diskindex.nDataPos;
//                pindexNew->nUndoPos       = diskindex.nUndoPos;
//                pindexNew->nVersion       = diskindex.nVersion;
//                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
//                pindexNew->nTime          = diskindex.nTime;
//                pindexNew->nBits          = diskindex.nBits;
//                pindexNew->nNonce         = diskindex.nNonce;
//                pindexNew->nStatus        = diskindex.nStatus;
//                pindexNew->nTx            = diskindex.nTx;

//                if (!CheckProofOfWork(pindexNew->nHeight, pindexNew->GetBlockHash(), pindexNew->nBits, Params().GetConsensus()))
//                    return error("%s: CheckProofOfWork failed: %s", __func__, pindexNew->ToString());

//                pcursor->Next();
//            } else {
//                return error("%s: failed to read value", __func__);
//            }
//        } else {
//            break;
//        }
//    }

//    return true;
//}

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
    s << strprintf("CSmartRewardEntry(height=%d, hash=%s, time=%d)\n",
        nHeight,
        blockHash.ToString(),
        blockTime);
    return s.str();
}
