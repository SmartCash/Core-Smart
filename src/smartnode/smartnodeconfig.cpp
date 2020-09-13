// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2020 - The SmartCash Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "netbase.h"
#include "smartnodeconfig.h"
#include "util.h"
#include "chainparams.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

CSmartnodeConfig smartnodeConfig;

std::string strHeader = "# Smartnode config file\n"
                        "# Format: alias IP:port smartnodeprivkey collateral_output_txid collateral_output_index\n"
                        "# Example: sn1 127.0.0.2:9678 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";


void CSmartnodeConfig::Load(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex) {
    CSmartnodeConfigEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

bool CSmartnodeConfig::Exists(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex, std::string& strErr){

    strErr = "";

    if( alias == "" ){
        strErr = "You need to provide a valid alias.";
        return false;
    }

    if( ip == "" ){
        strErr = "You need to provide a valid IP-Address.";
        return false;
    }

    auto aliasExists = std::find_if(entries.begin(), entries.end(), [alias](const CSmartnodeConfigEntry &entry) -> bool {
        return entry.getAlias() == alias;
    });

    if( aliasExists != entries.end() ){
        strErr += "- Alias is already in use.\n";
    }

    auto ipExists = std::find_if(entries.begin(), entries.end(), [ip](const CSmartnodeConfigEntry &entry) -> bool {
        return entry.getIp().find(ip) != std::string::npos;
    });

    if( ipExists != entries.end() ){
        strErr += "- IP-Address is already in use.\n";
    }

    auto keyExists = std::find_if(entries.begin(), entries.end(), [privKey](const CSmartnodeConfigEntry &entry) -> bool {
        return entry.getPrivKey() == privKey;
    });

    if( keyExists != entries.end() ){
        strErr += "- Smartnode Key is already in use.\n";
    }

    auto collateralExists = std::find_if(entries.begin(), entries.end(), [txHash, outputIndex](const CSmartnodeConfigEntry &entry) -> bool {
        return entry.getTxHash() == txHash && entry.getOutputIndex() == outputIndex;
    });

    if( collateralExists != entries.end() ){
        strErr += "- Collateral is already in use.\n";
    }

    return strErr != "";
}

bool CSmartnodeConfig::Read(std::string& strErr) {
    int linenumber = 1;
    boost::filesystem::path pathSmartnodeConfigFile = GetSmartnodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathSmartnodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathSmartnodeConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for(std::string line; std::getline(streamConfig, line); linenumber++)
    {
        if(line.empty()) continue;
        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if(comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }
        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                strErr = _("Could not parse smartnode.conf") + "\n" +
                        strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

        int port = 0;
        std::string hostname = "";
        SplitHostPort(ip, port, hostname);
        if(port == 0 || hostname == "") {
            strErr = _("Failed to parse host:port string") + "\n"+
                    strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
            streamConfig.close();
            return false;
        }
        int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
        LogPrintf("mainnetDefaultPort=%s\n", mainnetDefaultPort);
        LogPrintf("Params().NetworkIDString()=%s\n", Params().NetworkIDString());
        LogPrintf("CBaseChainParams::MAIN=%s\n", CBaseChainParams::MAIN);
        if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
            if(port != mainnetDefaultPort) {
                strErr = _("Invalid port detected in smartnode.conf") + "\n" +
                        strprintf(_("Port: %d"), port) + "\n" +
                        strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                        strprintf(_("(must be %d for mainnet)"), mainnetDefaultPort);
                streamConfig.close();
                return false;
            }
        } else if(port == mainnetDefaultPort) {
            strErr = _("Invalid port detected in smartnode.conf") + "\n" +
                    strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                    strprintf(_("(%d could be used only on mainnet)"), mainnetDefaultPort);
            streamConfig.close();
            return false;
        }

        if( Exists(alias, ip, privKey, txHash, outputIndex, strErr) ){
            strErr = _("Invalid entry detected in smartnode.conf") + "\n" +
                    strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                    strErr;
            return false;
        }

        Load(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}

bool CSmartnodeConfig::Write(std::string& strErr){

    std::string configString = strHeader;
    strErr = "";

    for( auto entry : entries)
        configString += strprintf("%s %s %s %s %s\n",entry.getAlias(),
                                                     entry.getIp(),
                                                     entry.getPrivKey(),
                                                     entry.getTxHash(),
                                                     entry.getOutputIndex());


    boost::filesystem::path pathSmartnodeConfigFile = GetSmartnodeConfigFile();
    FILE* configFile = fopen(pathSmartnodeConfigFile.string().c_str(), "w");

    if (configFile != NULL) {
        fwrite(configString.c_str(), std::strlen(configString.c_str()), 1, configFile);
        fclose(configFile);
        return true;
    }

    strErr = strprintf("Could not open file: %s",pathSmartnodeConfigFile.string());

    return false;
}

bool CSmartnodeConfig::Create(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex, std::string& strErr){

    if(Exists(alias, ip, privKey, txHash, outputIndex, strErr)) return false;

    // No error here.. now add the entry and save the config.
    Load(alias,ip,privKey,txHash,outputIndex);

    return Write(strErr);
}

bool CSmartnodeConfig::Edit(int index, std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex, std::string& strErr){

    CSmartnodeConfigEntry &entry = entries.at(index);
    CSmartnodeConfigEntry entryOld = entry;

    entry.setAlias("");
    entry.setIp("");
    entry.setPrivKey("");
    entry.setTxHash("");
    entry.setOutputIndex("");

    if(Exists(alias, ip, privKey, txHash, outputIndex, strErr)){
        entry.setAlias(entryOld.getAlias());
        entry.setIp(entryOld.getIp());
        entry.setPrivKey(entryOld.getPrivKey());
        entry.setTxHash(entryOld.getTxHash());
        entry.setOutputIndex(entryOld.getOutputIndex());
        return false;
    }

    entry.setAlias(alias);
    entry.setIp(ip);
    entry.setPrivKey(privKey);
    entry.setTxHash(txHash);
    entry.setOutputIndex(outputIndex);

    return Write(strErr);
}

bool CSmartnodeConfig::Remove(std::string privKey, std::string& strErr){

    auto entryExists = std::find_if(entries.begin(), entries.end(), [privKey](const CSmartnodeConfigEntry &entry) -> bool {
        return entry.getPrivKey() == privKey;
    });

    if( entryExists == entries.end() ){
        return false;
    }

    entries.erase(entryExists);

    return Write(strErr);
}
