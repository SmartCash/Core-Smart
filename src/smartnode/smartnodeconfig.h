// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2020 - The SmartCash Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SRC_SMARTNODECONFIG_H_
#define SRC_SMARTNODECONFIG_H_

#include <string>

class CSmartnodeConfig;
extern CSmartnodeConfig smartnodeConfig;

class CSmartnodeConfigEntry {

private:
    std::string alias;
    std::string ip;
    std::string privKey;
    std::string txHash;
    std::string outputIndex;
public:

    CSmartnodeConfigEntry(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex) {
        this->alias = alias;
        this->ip = ip;
        this->privKey = privKey;
        this->txHash = txHash;
        this->outputIndex = outputIndex;
    }

    const std::string& getAlias() const {
        return alias;
    }

    void setAlias(const std::string& alias) {
        this->alias = alias;
    }

    const std::string& getOutputIndex() const {
        return outputIndex;
    }

    void setOutputIndex(const std::string& outputIndex) {
        this->outputIndex = outputIndex;
    }

    const std::string& getPrivKey() const {
        return privKey;
    }

    void setPrivKey(const std::string& privKey) {
        this->privKey = privKey;
    }

    const std::string& getTxHash() const {
        return txHash;
    }

    void setTxHash(const std::string& txHash) {
        this->txHash = txHash;
    }

    const std::string& getIp() const {
        return ip;
    }

    void setIp(const std::string& ip) {
        this->ip = ip;
    }
};

class CSmartnodeConfig
{

public:

    CSmartnodeConfig() {
        entries = std::vector<CSmartnodeConfigEntry>();
    }

    void Clear();
    bool Read(std::string& strErr);
    bool Write(std::string& strErr);
    bool Exists(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex, std::string& strErr);
    void Load(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex);
    bool Create(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex, std::string& strErr);
    bool Edit(int index, std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex, std::string& strErr);
    bool Remove(std::string privKey, std::string& strErr);

    std::vector<CSmartnodeConfigEntry>& getEntries() {
        return entries;
    }

    int getCount() {
        return (int)entries.size();
    }

private:
    std::vector<CSmartnodeConfigEntry> entries;

};


#endif /* SRC_SMARTNODECONFIG_H_ */
