// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"

#include "chainparams.h"
#include "hash.h"
#include "pow.h"
#include "uint256.h"
#include "ui_interface.h"
#include "init.h"

#include <stdint.h>

#include <boost/thread.hpp>

using namespace std;

static const char DB_COIN = 'C';
static const char DB_COINS = 'c';
static const char DB_BLOCK_FILES = 'f';
static const char DB_TXINDEX = 't';
static const char DB_ADDRESSINDEX = 'a';
static const char DB_ADDRESSUNSPENTINDEX = 'u';
static const char DB_TIMESTAMPINDEX = 's';
static const char DB_SPENTINDEX = 'p';
static const char DB_DEPOSITINDEX = 'd';
static const char DB_BLOCK_INDEX = 'b';

static const char DB_VOTE_KEY_REGISTRATION = 'r';
static const char DB_VOTE_MAP_ADDRESS_TO_KEY = 'v';
static const char DB_VOTE_MAP_KEY_TO_ADDRESS = 'V';

static const char DB_INSTANTPAY_INDEX = 'i';

static const char DB_BEST_BLOCK = 'B';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';

namespace {

struct CoinEntry {
    COutPoint* outpoint;
    char key;
    CoinEntry(const COutPoint* ptr) : outpoint(const_cast<COutPoint*>(ptr)), key(DB_COIN)  {}

    template<typename Stream>
    void Serialize(Stream &s, int nType, int nVersion) const {
        s << key;
        s << outpoint->hash;
        s << VARINT(outpoint->n);
    }

    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        s >> key;
        s >> outpoint->hash;
        s >> VARINT(outpoint->n);
    }
};

}

CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe, true) 
{
}

bool CCoinsViewDB::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    return db.Read(CoinEntry(&outpoint), coin);
}

bool CCoinsViewDB::HaveCoin(const COutPoint &outpoint) const {
    return db.Exists(CoinEntry(&outpoint));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) {
    CDBBatch batch(db);
    size_t count = 0;
    size_t changed = 0;
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            CoinEntry entry(&it->first);
            if (it->second.coin.IsSpent())
                batch.Erase(entry);
            else
                batch.Write(entry, it->second.coin);
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }
    if (!hashBlock.IsNull())
        batch.Write(DB_BEST_BLOCK, hashBlock);

    bool ret = db.WriteBatch(batch);
    LogPrint("coindb", "Committed %u changed transaction outputs (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return ret;
}

size_t CCoinsViewDB::EstimateSize() const
{
    return db.EstimateSize(DB_COIN, (char)(DB_COIN+1));
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe) {
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

CCoinsViewCursor *CCoinsViewDB::Cursor() const
{
    CCoinsViewDBCursor *i = new CCoinsViewDBCursor(const_cast<CDBWrapper*>(&db)->NewIterator(), GetBestBlock());
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    i->pcursor->Seek(DB_COIN);
    // Cache key of first record
    if (i->pcursor->Valid()) {
        CoinEntry entry(&i->keyTmp.second);
        i->pcursor->GetKey(entry);
        i->keyTmp.first = entry.key;
    } else {
        i->keyTmp.first = 0; // Make sure Valid() and GetKey() return false
    }
    return i;
}

bool CCoinsViewDBCursor::GetKey(COutPoint &key) const
{
    // Return cached key
    if (keyTmp.first == DB_COIN) {
        key = keyTmp.second;
        return true;
    }
    return false;
}

bool CCoinsViewDBCursor::GetValue(Coin &coin) const
{
    return pcursor->GetValue(coin);
}

unsigned int CCoinsViewDBCursor::GetValueSize() const
{
    return pcursor->GetValueSize();
}

bool CCoinsViewDBCursor::Valid() const
{
    return keyTmp.first == DB_COIN;
}

void CCoinsViewDBCursor::Next()
{
    pcursor->Next();
    CoinEntry entry(&keyTmp.second);
    if (!pcursor->Valid() || !pcursor->GetKey(entry)) {
        keyTmp.first = 0; // Invalidate cached key after last record so that Valid() and GetKey() return false
    } else {
        keyTmp.first = entry.key;
    }
}

bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<int, const CBlockFileInfo*> >::const_iterator it=fileInfo.begin(); it != fileInfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos) {
    return Read(make_pair(DB_TXINDEX, txid), pos);
}

bool CBlockTreeDB::WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<uint256,CDiskTxPos> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_TXINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value) {
    return Read(make_pair(DB_SPENTINDEX, key), value);
}

bool CBlockTreeDB::UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CSpentIndexKey,CSpentIndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Erase(make_pair(DB_SPENTINDEX, it->first));
        } else {
            batch.Write(make_pair(DB_SPENTINDEX, it->first), it->second);
        }
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Erase(make_pair(DB_ADDRESSUNSPENTINDEX, it->first));
        } else {
            batch.Write(make_pair(DB_ADDRESSUNSPENTINDEX, it->first), it->second);
        }
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressUnspentIndexCount(uint160 addressHash, int type, int &nCount, CAddressUnspentKey &lastIndex) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ADDRESSUNSPENTINDEX, CAddressIndexIteratorKey(type, addressHash)));

    lastIndex.SetNull();
    nCount = 0;

    std::pair<char,CAddressUnspentKey> key;

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        if (pcursor->GetKey(key) && key.first == DB_ADDRESSUNSPENTINDEX && key.second.hashBytes == addressHash) {
            ++nCount;
            pcursor->Next();
        } else if( nCount ) {

            pcursor->Prev();

            if(pcursor->Valid() && pcursor->GetKey(key))
                lastIndex = key.second;

            break;
        }else{
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::ReadAddressUnspentIndex(uint160 addressHash, int type,
                                           std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs,
                                           const CAddressUnspentKey &start, int offset, int limit, bool reverse) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
    int nOffsetCount = 0, nFound = 0;

    if( start.IsNull() )
        pcursor->Seek(make_pair(DB_ADDRESSUNSPENTINDEX, CAddressIndexIteratorKey(type, addressHash)));
    else
        pcursor->Seek(make_pair(DB_ADDRESSUNSPENTINDEX, start));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CAddressUnspentKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ADDRESSUNSPENTINDEX && key.second.hashBytes == addressHash) {
            if (limit > 0 && nFound == limit) {
                break;
            }
            CAddressUnspentValue nValue;
            if (pcursor->GetValue(nValue)) {

                if( offset < 0 || ++nOffsetCount > offset ){
                    unspentOutputs.push_back(make_pair(key.second, nValue));
                    ++nFound;
                }

                if( reverse ) pcursor->Prev();
                else          pcursor->Next();

            } else {
                return error("failed to get address unspent value");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_ADDRESSINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_ADDRESSINDEX, it->first));
    return WriteBatch(batch);
}


bool CBlockTreeDB::ReadAddressIndex(uint160 addressHash, int type,
                                    std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
                                    int start, int end) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    if (start > 0 && end > 0) {
        pcursor->Seek(make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorHeightKey(type, addressHash, start)));
    } else {
        pcursor->Seek(make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorKey(type, addressHash)));
    }

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CAddressIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ADDRESSINDEX && key.second.hashBytes == addressHash) {
            if (end > 0 && key.second.blockHeight > end) {
                break;
            }
            CAmount nValue;
            if (pcursor->GetValue(nValue)) {
                addressIndex.push_back(make_pair(key.second, nValue));
                pcursor->Next();
            } else {
                return error("failed to get address index value");
            }
        } else {
            break;
        }
    }

    return true;
}


bool CBlockTreeDB::ReadAddresses(std::vector<CAddressListEntry> &addressList, int nEndHeight, bool excludeZeroBalances) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(DB_ADDRESSINDEX);

    CAddressIndexKey currentKey = CAddressIndexKey();
    CAmount currentReceived = 0, currentBalance = 0;

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CAddressIndexKey> key;
        if (pcursor->GetKey(key)){

            if( currentKey.IsNull() ){
                currentKey = key.second;
            }

            if( key.first != DB_ADDRESSINDEX)
                break;

            if( key.second.hashBytes != currentKey.hashBytes ) {

                if( currentBalance > 0 && (!excludeZeroBalances || (excludeZeroBalances && currentBalance )))
                    // Save the address info
                    addressList.push_back(CAddressListEntry(currentKey.type,
                                                            currentKey.hashBytes,
                                                            currentReceived,
                                                            currentBalance));

                // And move on with the next one
                currentReceived = 0;
                currentBalance = 0;
                currentKey = key.second;
            }

            CAmount nValue;
            if (pcursor->GetValue(nValue)) {

                if( nEndHeight == -1 || key.second.blockHeight < nEndHeight ){
                    currentBalance += nValue;
                    if( nValue > 0)
                        currentReceived += nValue;
                }

                pcursor->Next();
            } else {
                return error("failed to get address index value");
            }
        } else {
            break;
        }
    }

    if( !excludeZeroBalances || (excludeZeroBalances && currentBalance ))
        // Store the last one..
        addressList.push_back(CAddressListEntry(currentKey.type,
                                                currentKey.hashBytes,
                                                currentReceived,
                                                currentBalance));

    return true;
}

bool CBlockTreeDB::WriteTimestampIndex(const CTimestampIndexKey &timestampIndex) {
    CDBBatch batch(*this);
    batch.Write(make_pair(DB_TIMESTAMPINDEX, timestampIndex), 0);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadTimestampIndex(const unsigned int &high, const unsigned int &low, std::vector<uint256> &hashes) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_TIMESTAMPINDEX, CTimestampIndexIteratorKey(low)));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, CTimestampIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_TIMESTAMPINDEX && key.second.timestamp <= high) {
            hashes.push_back(key.second.blockHash);
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::ReadTimestampIndex(const unsigned int &timestamp, uint256 &blockHash) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_TIMESTAMPINDEX, CTimestampIndexIteratorKey(timestamp)));

    blockHash.SetNull();

    if (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, CTimestampIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_TIMESTAMPINDEX) {
            blockHash = key.second.blockHash;
            return true;
        }
    }

    return false;
}

bool CBlockTreeDB::WriteDepositIndex(const std::vector<std::pair<CDepositIndexKey, CDepositValue > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CDepositIndexKey, CDepositValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_DEPOSITINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseDepositIndex(const std::vector<std::pair<CDepositIndexKey, CDepositValue > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CDepositIndexKey, CDepositValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_DEPOSITINDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadDepositIndex(uint160 addressHash, int type,
                                    std::vector<std::pair<CDepositIndexKey, CDepositValue> > &depositIndex,
                                    int start, int offset, int limit, bool reverse) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    int nCount = 0;

    if (start > 0) {
        pcursor->Seek(make_pair(DB_DEPOSITINDEX, CDepositIndexIteratorTimeKey(type, addressHash, start)));
    } else {
        pcursor->Seek(make_pair(DB_DEPOSITINDEX, CDepositIndexIteratorKey(type, addressHash)));
    }

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CDepositIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_DEPOSITINDEX && key.second.hashBytes == addressHash) {
            if (limit > 0 && depositIndex.size() == (size_t)limit) {
                break;
            }
            CDepositValue nValue;
            if (pcursor->GetValue(nValue)) {
                if( ++nCount > offset )
                    depositIndex.push_back(make_pair(key.second, nValue));

                if( reverse ) pcursor->Prev();
                else          pcursor->Next();

            } else {
                return error("failed to get deposit index value");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::ReadDepositIndexCount(uint160 addressHash, int type,
                                    int &count,
                                    int &firstTime, int &lastTime,
                                    int start, int end) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    count = 0;
    firstTime = 0;
    lastTime = 0;

    if (start > 0) {
        pcursor->Seek(make_pair(DB_DEPOSITINDEX, CDepositIndexIteratorTimeKey(type, addressHash, start)));
    } else {
        pcursor->Seek(make_pair(DB_DEPOSITINDEX, CDepositIndexIteratorKey(type, addressHash)));
    }

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CDepositIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_DEPOSITINDEX && key.second.hashBytes == addressHash) {

            if( !firstTime ) firstTime = key.second.timestamp;

            if (end > 0 && key.second.timestamp > (unsigned int)end) {
                if( !lastTime ) lastTime = firstTime;
                break;
            }

            lastTime = key.second.timestamp;
            count++;
            pcursor->Next();

        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteInstantPayLocks(std::map<CInstantPayIndexKey, CInstantPayValue> &mapLocks)
{
    CDBBatch batch(*this);
    for (auto& lock : mapLocks ){

        if( lock.second.fProcessed && !lock.second.fWritten ){
            lock.second.fWritten = true;
            batch.Write(make_pair(DB_INSTANTPAY_INDEX, lock.first), lock.second);
        }
    }
    return batch.SizeEstimate() ? WriteBatch(batch) : true;
}

bool CBlockTreeDB::ReadInstantPayIndex( std::vector<std::pair<CInstantPayIndexKey, CInstantPayValue> > &instantPayIndex,
                                        int start, int offset, int limit, bool reverse) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    int nCount = 0;

    if (start > 0) {
        pcursor->Seek(make_pair(DB_INSTANTPAY_INDEX, CInstantPayIndexIteratorTimeKey(start)));
    } else {
        pcursor->Seek(DB_INSTANTPAY_INDEX);
    }

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CInstantPayIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_INSTANTPAY_INDEX) {
            if (limit > 0 && instantPayIndex.size() == (size_t)limit) {
                break;
            }
            CInstantPayValue nValue;
            if (pcursor->GetValue(nValue)) {
                if( ++nCount > offset )
                    instantPayIndex.push_back(make_pair(key.second, nValue));

                if( reverse ) pcursor->Prev();
                else          pcursor->Next();

            } else {
                return error("failed to get instantpay index value");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::ReadInstantPayIndexCount(int &count, int &firstTime, int &lastTime,
                                            int start, int end) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    count = 0;
    firstTime = 0;
    lastTime = 0;

    if (start > 0) {
        pcursor->Seek(make_pair(DB_INSTANTPAY_INDEX, CInstantPayIndexIteratorTimeKey(start)));
    } else {
        pcursor->Seek(DB_INSTANTPAY_INDEX);
    }

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CInstantPayIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_INSTANTPAY_INDEX) {

            if( !firstTime ) firstTime = key.second.timestamp;

            if (end > 0 && key.second.timestamp > (unsigned int)end) {
                if( !lastTime ) lastTime = firstTime;
                break;
            }

            lastTime = key.second.timestamp;
            count++;
            pcursor->Next();

        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteInvalidVoteKeyRegistrations(std::vector<std::pair<CVoteKeyRegistrationKey, VoteKeyParseResult>> vecInvalidRegistrations)
{
    CDBBatch batch(*this);

    int val;

    for( auto reg : vecInvalidRegistrations ){
        val = reg.second;
        batch.Write(make_pair(DB_VOTE_KEY_REGISTRATION, reg.first), val);
    }

    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseInvalidVoteKeyRegistrations(std::vector<CVoteKeyRegistrationKey> vecInvalidRegistrations)
{
    CDBBatch batch(*this);

    for( auto reg : vecInvalidRegistrations ){
        batch.Erase(make_pair(DB_VOTE_KEY_REGISTRATION, reg));
    }

    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadInvalidVoteKeyRegistration(const uint256 &txHash, CVoteKeyRegistrationKey &registrationKey, VoteKeyParseResult &result)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(DB_VOTE_KEY_REGISTRATION);

    while (pcursor->Valid()) {

        std::pair<char,CVoteKeyRegistrationKey> key;

        if (pcursor->GetKey(key) && key.first == DB_VOTE_KEY_REGISTRATION) {

            if( key.second.nTxHash == txHash ){

                int nValue;
                if ( !pcursor->GetValue(nValue) ) {
                    return error("failed to get VoteKey registration value");
                }

                result = (VoteKeyParseResult)nValue;
                registrationKey = key.second;

                return true;
            }

            pcursor->Next();

        }else {
            break;
        }
    }

    return false;
}

bool CBlockTreeDB::WriteVoteKeys(const std::map<CVoteKey, CVoteKeyValue> &mapVoteKeys)
{
    CDBBatch batch(*this);

    for( auto it : mapVoteKeys ){
        batch.Write(make_pair(DB_VOTE_MAP_ADDRESS_TO_KEY, it.second.voteAddress), it.first);
        batch.Write(make_pair(DB_VOTE_MAP_KEY_TO_ADDRESS, it.first), it.second);
    }

    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseVoteKeys(const std::map<CVoteKey, CSmartAddress> &mapVoteKeys)
{
    CDBBatch batch(*this);

    for( auto it : mapVoteKeys ){
        batch.Erase(make_pair(DB_VOTE_MAP_ADDRESS_TO_KEY, it.second));
        batch.Erase(make_pair(DB_VOTE_MAP_KEY_TO_ADDRESS, it.first));
    }

    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadVoteKeyForAddress(const CSmartAddress &voteAddress, CVoteKey &voteKey)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_VOTE_MAP_ADDRESS_TO_KEY, voteAddress));

    if (pcursor->Valid()) {

        std::pair<char,CSmartAddress> key;

        if (pcursor->GetKey(key) && key.first == DB_VOTE_MAP_ADDRESS_TO_KEY && key.second == voteAddress) {

            if (!pcursor->GetValue(voteKey)) {
                return error("failed to get vote key");
            }

            return true;
        }
    }

    return false;
}


bool CBlockTreeDB::ReadVoteKeys(std::vector<std::pair<CVoteKey,CVoteKeyValue>> &vecVoteKeys)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(DB_VOTE_MAP_KEY_TO_ADDRESS);

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, CVoteKey> key;
        if (pcursor->GetKey(key) && key.first == DB_VOTE_MAP_KEY_TO_ADDRESS) {

            CVoteKeyValue nValue;

            if (pcursor->GetValue(nValue)) {

                vecVoteKeys.push_back(std::make_pair(key.second,nValue));

            } else {
                return error("failed to get vote key value");
            }

            pcursor->Next();

        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::ReadVoteKeyValue(const CVoteKey &voteKey, CVoteKeyValue &voteKeyValue)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_VOTE_MAP_KEY_TO_ADDRESS, voteKey));

    if (pcursor->Valid()) {

        std::pair<char,CSmartAddress> key;

        if (pcursor->GetKey(key) && key.first == DB_VOTE_MAP_KEY_TO_ADDRESS && key.second == voteKey) {

            if (!pcursor->GetValue(voteKeyValue)) {
                return error("failed to get vote key value");
            }

            return true;
        }
    }

    return false;
}


bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts(boost::function<CBlockIndex*(const uint256&)> insertBlockIndex)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_BLOCK_INDEX, uint256()));

    // Load mapBlockIndex
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
            CDiskBlockIndex diskindex;
            if (pcursor->GetValue(diskindex)) {
                // Construct block index object
                CBlockIndex* pindexNew = insertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev          = insertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight        = diskindex.nHeight;
                pindexNew->nFile          = diskindex.nFile;
                pindexNew->nDataPos       = diskindex.nDataPos;
                pindexNew->nUndoPos       = diskindex.nUndoPos;
                pindexNew->nVersion       = diskindex.nVersion;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->nTime          = diskindex.nTime;
                pindexNew->nBits          = diskindex.nBits;
                pindexNew->nNonce         = diskindex.nNonce;
                pindexNew->nStatus        = diskindex.nStatus;
                pindexNew->nTx            = diskindex.nTx;

                if (!CheckProofOfWork(pindexNew->nHeight, pindexNew->GetBlockHash(), pindexNew->nBits, Params().GetConsensus()))
                    return error("%s: CheckProofOfWork failed: %s", __func__, pindexNew->ToString());

                pcursor->Next();
            } else {
                return error("%s: failed to read value", __func__);
            }
        } else {
            break;
        }
    }

    return true;
}

namespace {

//! Legacy class to deserialize pre-pertxout database entries without reindex.
class CCoins
{
public:
    //! whether transaction is a coinbase
    bool fCoinBase;

    //! unspent transaction outputs; spent outputs are .IsNull(); spent outputs at the end of the array are dropped
    std::vector<CTxOut> vout;

    //! at which height this transaction was included in the active block chain
    int nHeight;

    //! empty constructor
    CCoins() : fCoinBase(false), vout(0), nHeight(0) { }

    template<typename Stream>
    void Unserialize(Stream &s, int nType, int nVersion) {
        unsigned int nCode = 0;
        // version
        int nVersionDummy;
        ::Unserialize(s, VARINT(nVersionDummy), nType, nVersion);
        // header code
        ::Unserialize(s, VARINT(nCode), nType, nVersion);
        fCoinBase = nCode & 1;
        std::vector<bool> vAvail(2, false);
        vAvail[0] = (nCode & 2) != 0;
        vAvail[1] = (nCode & 4) != 0;
        unsigned int nMaskCode = (nCode / 8) + ((nCode & 6) != 0 ? 0 : 1);
        // spentness bitmask
        while (nMaskCode > 0) {
            unsigned char chAvail = 0;
            ::Unserialize(s, chAvail, nType, nVersion);
            for (unsigned int p = 0; p < 8; p++) {
                bool f = (chAvail & (1 << p)) != 0;
                vAvail.push_back(f);
            }
            if (chAvail != 0)
                nMaskCode--;
        }
        // txouts themself
        vout.assign(vAvail.size(), CTxOut());
        for (unsigned int i = 0; i < vAvail.size(); i++) {
            if (vAvail[i])
                ::Unserialize(s, REF(CTxOutCompressor(vout[i])), nType, nVersion);
        }
        // coinbase height
        ::Unserialize(s, VARINT(nHeight), nType, nVersion);
    }
};

}

/** Upgrade the database from older formats.
 *
 * Currently implemented: from the per-tx utxo model (0.8..0.14.x) to per-txout.
 */
bool CCoinsViewDB::Upgrade() {
    std::unique_ptr<CDBIterator> pcursor(db.NewIterator());
    pcursor->Seek(std::make_pair(DB_COINS, uint256()));
    if (!pcursor->Valid()) {
        return true;
    }

    int64_t count = 0;
    LogPrintf("Upgrading utxo-set database...\n");
    LogPrintf("[0%%]...");
    size_t batch_size = 1 << 24;
    CDBBatch batch(db);
    uiInterface.SetProgressBreakAction(StartShutdown);
    int reportDone = 0;
    std::pair<unsigned char, uint256> key;
    std::pair<unsigned char, uint256> prev_key = {DB_COINS, uint256()};
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        if (ShutdownRequested()) {
            break;
        }
        if (pcursor->GetKey(key) && key.first == DB_COINS) {
            if (count++ % 256 == 0) {
                uint32_t high = 0x100 * *key.second.begin() + *(key.second.begin() + 1);
                int percentageDone = (int)(high * 100.0 / 65536.0 + 0.5);
                uiInterface.ShowProgress(_("Upgrading UTXO database") + "\n"+ _("(press q to shutdown and continue later)") + "\n", percentageDone);
                if (reportDone < percentageDone/10) {
                    // report max. every 10% step
                    LogPrintf("[%d%%]...", percentageDone);
                    reportDone = percentageDone/10;
                }
            }
            CCoins old_coins;
            if (!pcursor->GetValue(old_coins)) {
                return error("%s: cannot parse CCoins record", __func__);
            }
            COutPoint outpoint(key.second, 0);
            for (size_t i = 0; i < old_coins.vout.size(); ++i) {
                if (!old_coins.vout[i].IsNull() && !old_coins.vout[i].scriptPubKey.IsUnspendable()) {
                    Coin newcoin(std::move(old_coins.vout[i]), old_coins.nHeight, old_coins.fCoinBase);
                    outpoint.n = i;
                    CoinEntry entry(&outpoint);
                    batch.Write(entry, newcoin);
                }
            }
            batch.Erase(key);
            if (batch.SizeEstimate() > batch_size) {
                db.WriteBatch(batch);
                batch.Clear();
                db.CompactRange(prev_key, key);
                prev_key = key;
            }
            pcursor->Next();
        } else {
            break;
        }
    }
    db.WriteBatch(batch);
    db.CompactRange({DB_COINS, uint256()}, key);
    uiInterface.SetProgressBreakAction(std::function<void(void)>());
    LogPrintf("[%s].\n", ShutdownRequested() ? "CANCELLED" : "DONE");
    return !ShutdownRequested();
}
