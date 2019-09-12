// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SPENTINDEX_H
#define BITCOIN_SPENTINDEX_H

#include "uint256.h"
#include "amount.h"
#include "script/script.h"
#include "smarthive/hive.h"

struct CSpentIndexKey {
    uint256 txid;
    unsigned int outputIndex;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(txid);
        READWRITE(outputIndex);
    }

    CSpentIndexKey(uint256 t, unsigned int i) {
        txid = t;
        outputIndex = i;
    }

    CSpentIndexKey() {
        SetNull();
    }

    void SetNull() {
        txid.SetNull();
        outputIndex = 0;
    }

};

struct CSpentIndexValue {
    uint256 txid;
    unsigned int inputIndex;
    int blockHeight;
    CAmount satoshis;
    int addressType;
    uint160 addressHash;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(txid);
        READWRITE(inputIndex);
        READWRITE(blockHeight);
        READWRITE(satoshis);
        READWRITE(addressType);
        READWRITE(addressHash);
    }

    CSpentIndexValue(uint256 t, unsigned int i, int h, CAmount s, int type, uint160 a) {
        txid = t;
        inputIndex = i;
        blockHeight = h;
        satoshis = s;
        addressType = type;
        addressHash = a;
    }

    CSpentIndexValue() {
        SetNull();
    }

    void SetNull() {
        txid.SetNull();
        inputIndex = 0;
        blockHeight = 0;
        satoshis = 0;
        addressType = 0;
        addressHash.SetNull();
    }

    bool IsNull() const {
        return txid.IsNull();
    }
};

struct CSpentIndexKeyCompare
{
    bool operator()(const CSpentIndexKey& a, const CSpentIndexKey& b) const {
        if (a.txid == b.txid) {
            return a.outputIndex < b.outputIndex;
        } else {
            return a.txid < b.txid;
        }
    }
};

struct CTimestampIndexIteratorKey {
    unsigned int timestamp;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 4;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata32be(s, timestamp);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        timestamp = ser_readdata32be(s);
    }

    CTimestampIndexIteratorKey(unsigned int time) {
        timestamp = time;
    }

    CTimestampIndexIteratorKey() {
        SetNull();
    }

    void SetNull() {
        timestamp = 0;
    }
};

struct CTimestampIndexKey {
    unsigned int timestamp;
    uint256 blockHash;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 36;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata32be(s, timestamp);
        blockHash.Serialize(s, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        timestamp = ser_readdata32be(s);
        blockHash.Unserialize(s, nType, nVersion);
    }

    CTimestampIndexKey(unsigned int time, uint256 hash) {
        timestamp = time;
        blockHash = hash;
    }

    CTimestampIndexKey() {
        SetNull();
    }

    void SetNull() {
        timestamp = 0;
        blockHash.SetNull();
    }
};

struct CAddressUnspentKey {
    unsigned int type;
    uint160 hashBytes;
    int nBlockHeight;
    uint256 txhash;
    size_t index;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 61;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
        ser_writedata32be(s, nBlockHeight);
        txhash.Serialize(s, nType, nVersion);
        ser_writedata32(s, index);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
        nBlockHeight = ser_readdata32be(s);
        txhash.Unserialize(s, nType, nVersion);
        index = ser_readdata32(s);
    }

    CAddressUnspentKey(unsigned int addressType, uint160 addressHash, uint256 txid, size_t indexValue, int blockHeight) {
        type = addressType;
        hashBytes = addressHash;
        nBlockHeight = blockHeight;
        txhash = txid;
        index = indexValue;
    }

    CAddressUnspentKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        nBlockHeight = -1;
        txhash.SetNull();
        index = 0;
    }

    bool IsNull() const { return hashBytes.IsNull(); }

    friend bool operator==(const CAddressUnspentKey& a, const CAddressUnspentKey& b)
    {
        return a.type == b.type &&
               a.hashBytes == b.hashBytes &&
               a.txhash == b.txhash &&
               a.index == b.index;
    }
};

struct CAddressUnspentValue {
    CAmount satoshis;
    CScript script;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(satoshis);
        READWRITE(*(CScriptBase*)(&script));
    }

    CAddressUnspentValue(CAmount sats, CScript scriptPubKey, int height) {
        satoshis = sats;
        script = scriptPubKey;
    }

    CAddressUnspentValue() {
        SetNull();
    }

    void SetNull() {
        satoshis = -1;
        script.clear();
    }

    bool IsNull() const {
        return (satoshis == -1);
    }
};

struct CAddressIndexKey {
    unsigned int type;
    uint160 hashBytes;
    int blockHeight;
    unsigned int txindex;
    uint256 txhash;
    size_t index;
    bool spending;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 66;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
        // Heights are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, blockHeight);
        ser_writedata32be(s, txindex);
        txhash.Serialize(s, nType, nVersion);
        ser_writedata32(s, index);
        char f = spending;
        ser_writedata8(s, f);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
        blockHeight = ser_readdata32be(s);
        txindex = ser_readdata32be(s);
        txhash.Unserialize(s, nType, nVersion);
        index = ser_readdata32(s);
        char f = ser_readdata8(s);
        spending = f;
    }

    CAddressIndexKey(unsigned int addressType, uint160 addressHash, int height, int blockindex,
                     uint256 txid, size_t indexValue, bool isSpending) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
        txindex = blockindex;
        txhash = txid;
        index = indexValue;
        spending = isSpending;
    }

    CAddressIndexKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
        txindex = 0;
        txhash.SetNull();
        index = 0;
        spending = false;
    }

    bool IsNull(){ return hashBytes.IsNull(); }
};

struct CAddressIndexIteratorKey {
    unsigned int type;
    uint160 hashBytes;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 21;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
    }

    CAddressIndexIteratorKey(unsigned int addressType, uint160 addressHash) {
        type = addressType;
        hashBytes = addressHash;
    }

    CAddressIndexIteratorKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
    }
};

struct CAddressIndexIteratorHeightKey {
    unsigned int type;
    uint160 hashBytes;
    int blockHeight;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 25;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
        ser_writedata32be(s, blockHeight);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
        blockHeight = ser_readdata32be(s);
    }

    CAddressIndexIteratorHeightKey(unsigned int addressType, uint160 addressHash, int height) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
    }

    CAddressIndexIteratorHeightKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
    }
};

struct CAddressListEntry {
    unsigned int type;
    uint160 hashBytes;
    CAmount received;
    CAmount balance;

    CAddressListEntry(unsigned int addressType, uint160 addressHash, CAmount nReceived, CAmount nBalance) {
        type = addressType;
        hashBytes = addressHash;
        received = nReceived;
        balance = nBalance;
    }

    CAddressListEntry() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        received = 0;
        balance = 0;
    }

    bool IsNull(){ return hashBytes.IsNull(); }
};

struct CDepositIndexKey {
    unsigned int type;
    uint160 hashBytes;
    unsigned int timestamp;
    uint256 txhash;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 57;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
        // Timestamps are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, timestamp);
        txhash.Serialize(s, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
        timestamp = ser_readdata32be(s);
        txhash.Unserialize(s, nType, nVersion);
    }

    CDepositIndexKey(unsigned int addressType, uint160 addressHash,
                     int time, uint256 txid) {
        type = addressType;
        hashBytes = addressHash;
        timestamp = time;
        txhash = txid;
    }

    CDepositIndexKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        timestamp = 0;
        txhash.SetNull();
    }

};

struct CDepositValue {
    CAmount satoshis;
    int blockHeight;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(satoshis);
        READWRITE(blockHeight);
    }

    CDepositValue(CAmount sats, int height) {
        satoshis = sats;
        blockHeight = height;
    }

    CDepositValue() {
        SetNull();
    }

    void SetNull() {
        satoshis = -1;
        blockHeight = 0;
    }

    bool IsNull() const {
        return (satoshis == -1);
    }
};


struct CDepositIndexIteratorKey {
    unsigned int type;
    uint160 hashBytes;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 21;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
    }

    CDepositIndexIteratorKey(unsigned int addressType, uint160 addressHash) {
        type = addressType;
        hashBytes = addressHash;
    }

    CDepositIndexIteratorKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
    }
};

struct CDepositIndexIteratorTimeKey {
    unsigned int type;
    uint160 hashBytes;
    int timestamp;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 25;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
        ser_writedata32be(s, timestamp);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
        timestamp = ser_readdata32be(s);
    }

    CDepositIndexIteratorTimeKey(unsigned int addressType, uint160 addressHash, int time) {
        type = addressType;
        hashBytes = addressHash;
        timestamp = time;
    }

    CDepositIndexIteratorTimeKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        timestamp = 0;
    }
};


struct CVoteKeyRegistrationKey {
    int nHeight;
    uint256 nTxHash;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 24;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        // Timestamps are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, nHeight);
        nTxHash.Serialize(s, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        nHeight = ser_readdata32be(s);
        nTxHash.Unserialize(s, nType, nVersion);
    }

    CVoteKeyRegistrationKey(const int nBlockHeight, const uint256 &nTxHash):
        nHeight(nBlockHeight),
        nTxHash(nTxHash)
    { }

    CVoteKeyRegistrationKey() {
        SetNull();
    }

    void SetNull() {
        nHeight = -1;
        nTxHash.SetNull();
    }

    bool IsNull() const {
        return nTxHash.IsNull() || nHeight == -1;
    }

    std::string ToString() const{
        return strprintf("CVoteKeyRegistrationKey(txhash=%s, height=%d", nTxHash.ToString(), nHeight);
    }
};


struct CVoteKeyRegistrationValue {
    CVoteKey voteKey;
    bool fProcessed;
    bool fValid;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(voteKey);
        READWRITE(fProcessed);
        READWRITE(fValid);
    }

    CVoteKeyRegistrationValue(const CVoteKey &voteKey, const bool fProcessed, const bool fValid) :
                             voteKey(voteKey),
                             fProcessed(fProcessed),
                             fValid(fValid) {

    }

    CVoteKeyRegistrationValue() {
        SetNull();
    }

    void SetNull() {
        voteKey.SetString("invalid");
        fProcessed = false;
        fValid = false;
    }

    bool IsValid() { return voteKey.IsValid(); }

    std::string ToString() const{
        return strprintf("CVoteKeyRegistrationValue(votekey=%s, processed=%d valid=%b", voteKey.ToString(), fProcessed, fValid);
    }
};

struct CVoteKeyValue {

    CSmartAddress voteAddress;
    uint256 nTxHash;
    int nBlockHeight;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(voteAddress);
        READWRITE(nTxHash);
        READWRITE(nBlockHeight);
    }

    CVoteKeyValue(const CSmartAddress &voteAddress, const uint256 &nTxHash, int nBlockHeight):
        voteAddress(voteAddress),
        nTxHash(nTxHash),
        nBlockHeight(nBlockHeight)
    { }

    CVoteKeyValue() {
        SetNull();
    }

    void SetNull() {
        voteAddress = CSmartAddress();
        nTxHash.SetNull();
        nBlockHeight = -1;
    }

    bool IsNull() const {
        return (nBlockHeight == -1);
    }

    bool IsValid(){
        return voteAddress.IsValid();
    }

    std::string ToString() const{
        return strprintf("CVoteKeyValue(address=%s, txhash=%s, height=%d", voteAddress.ToString(), nTxHash.ToString(), nBlockHeight);
    }
};

struct CInstantPayIndexKey {
    unsigned int timestamp;
    uint256 txhash;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 36;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        // Timestamps are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, timestamp);
        txhash.Serialize(s, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        timestamp = ser_readdata32be(s);
        txhash.Unserialize(s, nType, nVersion);
    }

    CInstantPayIndexKey(unsigned int time, uint256 txid) {
        timestamp = time;
        txhash = txid;
    }

    CInstantPayIndexKey() {
        SetNull();
    }

    void SetNull() {
        timestamp = 0;
        txhash.SetNull();
    }

    friend inline bool operator==(const CInstantPayIndexKey& a, const CInstantPayIndexKey& b) {
        return a.timestamp == b.timestamp && a.txhash == b.txhash;
    }
    friend inline bool operator!=(const CInstantPayIndexKey& a, const CInstantPayIndexKey& b) {
        return !(a == b);
    }
    friend inline bool operator<(const CInstantPayIndexKey& a, const CInstantPayIndexKey& b) {
        return a.timestamp == b.timestamp ? a.txhash < b.txhash : a.timestamp < b.timestamp;;
    }

};

struct CInstantPayValue {
    bool fProcessed; // Internal only
    bool fWritten; // Internal only
    int64_t timeCreated; // Internal only

    bool fValid;
    int receivedLocks;
    int maxLocks;
    int elapsedTime;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(fValid);
        READWRITE(receivedLocks);
        READWRITE(maxLocks);
        READWRITE(elapsedTime);
    }

    CInstantPayValue(bool valid, int receivedLocks, int maxLocks, int elapsedTime) :
                                                           fProcessed(false),
                                                           fWritten(false),
                                                           fValid(valid),
                                                           receivedLocks(receivedLocks),
                                                           maxLocks(maxLocks),
                                                           elapsedTime(elapsedTime) {

    }

    CInstantPayValue() {
        SetNull();
    }

    CInstantPayValue(int64_t nTimeCreated) {
        SetNull();
        timeCreated = nTimeCreated;
    }

    void SetNull() {
        fProcessed = false;
        fWritten = false;
        timeCreated = 0;

        fValid = false;
        receivedLocks = -1;
        maxLocks = -1;
        elapsedTime = 0;
    }

    bool IsNull() const {
        return (receivedLocks == -1);
    }
};


struct CInstantPayIndexIteratorTimeKey {
    unsigned int timestamp;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 4;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata32be(s, timestamp);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        timestamp = ser_readdata32be(s);
    }

    CInstantPayIndexIteratorTimeKey(int time) {
        timestamp = time;
    }

    CInstantPayIndexIteratorTimeKey() {
        SetNull();
    }

    void SetNull() {
        timestamp = 0;
    }
};


#endif // BITCOIN_SPENTINDEX_H
