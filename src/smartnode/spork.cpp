// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "../chainparams.h"
#include "../validation.h"
#include "../messagesigner.h"
#include "../net_processing.h"
#include "spork.h"

#include <boost/lexical_cast.hpp>

class CSporkMessage;
class CSporkManager;

CSporkManager sporkManager;

std::map<uint256, CSporkMessage> mapSporks;
std::map<int, int64_t> mapSporkDefaults = {
    {SPORK_2_INSTANTSEND_ENABLED,               0},             // ON
    {SPORK_3_INSTANTSEND_BLOCK_FILTERING,       0},             // ON
    {SPORK_5_INSTANTSEND_MAX_VALUE,             100000},          // 1000 SMART
    {SPORK_8_SMARTNODE_PAYMENT_ENFORCEMENT,     1551316010}, // OFF until Feb 28 but will activate sooner
    {SPORK_15_SMARTREWARDS_BLOCKS_ENABLED,      INT_MAX}, // ON
    {SPORK_16_MINING_SIGNATURE_ENFORCEMENT,     552300}, // OFF until block 552300
    {SPORK_17_MINING_SIGNATURE_PUBKEYS_ENABLED, 0xFFFFFFFFFFFFFFFF}, // All enabled
    {SPORK_18_PAY_OUTREACH2,                    0}, // ON until block number  This fork cannot be reversed
    {SPORK_19_PAY_WEB,                          0}, // ON until block number  This fork cannot be reversed
    {SPORK_20_PAY_QUALITY,                      0}, // ON until block number  This fork cannot be reversed
    {SPORK_21_SMARTNODE_PROTOCOL_REQUIREMENT,   0xF2A5238000001C1B}, // byte0 = old protocol, byte1 = new protocol, bytes 2-7 enable time
};

void CSporkManager::ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{

    if (strCommand == NetMsgType::SPORK) {

        CSporkMessage spork;
        vRecv >> spork;

        uint256 hash = spork.GetHash();

        std::string strLogMsg;
        {
            LOCK(cs_main);
            pfrom->setAskFor.erase(hash);
            if(!chainActive.Tip()) return;
            strLogMsg = strprintf("SPORK -- hash: %s id: %d value: %10d bestHeight: %d peer=%d", hash.ToString(), spork.nSporkID, spork.nValue, chainActive.Height(), pfrom->id);
        }

        if(mapSporksActive.count(spork.nSporkID)) {
            if (mapSporksActive[spork.nSporkID].nTimeSigned >= spork.nTimeSigned) {
                LogPrint("spork", "%s seen\n", strLogMsg);
                return;
            } else {
                LogPrintf("%s updated\n", strLogMsg);
            }
        } else {
            LogPrintf("%s new\n", strLogMsg);
        }

        if(!spork.CheckSignature(sporkPubKeyID)) {
            LOCK(cs_main);
            LogPrintf("CSporkManager::ProcessSpork -- ERROR: invalid signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        mapSporks[hash] = spork;
        mapSporksActive[spork.nSporkID] = spork;
        spork.Relay(connman);

        //does a task if needed
        ExecuteSpork(spork.nSporkID, spork.nValue);

    } else if (strCommand == NetMsgType::GETSPORKS) {

        std::map<int, CSporkMessage>::iterator it = mapSporksActive.begin();

        while(it != mapSporksActive.end()) {
            connman.PushMessage(pfrom, NetMsgType::SPORK, it->second);
            it++;
        }
    }

}

void CSporkManager::ExecuteSpork(int nSporkID, int nValue)
{

}

bool CSporkManager::UpdateSpork(int nSporkID, int64_t nValue, CConnman& connman)
{

    CSporkMessage spork = CSporkMessage(nSporkID, nValue, GetAdjustedTime());

    if(spork.Sign(sporkPrivKey)) {
        spork.Relay(connman);
        mapSporks[spork.GetHash()] = spork;
        mapSporksActive[nSporkID] = spork;
        return true;
    }

    return false;
}

// grab the spork, otherwise say it's off
bool CSporkManager::IsSporkActive(int nSporkID)
{
    int64_t r = -1;

    if(mapSporksActive.count(nSporkID)){
        r = mapSporksActive[nSporkID].nValue;
    } else if (mapSporkDefaults.count(nSporkID)) {
        r = mapSporkDefaults[nSporkID];
    } else {
        LogPrint("spork", "CSporkManager::IsSporkActive -- Unknown Spork ID %d\n", nSporkID);
        r = 4070908800ULL; // 2099-1-1 i.e. off by default
    }

    return r < GetAdjustedTime();
}

// grab the value of the spork on the network, or the default
int64_t CSporkManager::GetSporkValue(int nSporkID)
{
    if (mapSporksActive.count(nSporkID))
        return mapSporksActive[nSporkID].nValue;

    if (mapSporkDefaults.count(nSporkID)) {
        return mapSporkDefaults[nSporkID];
    }

    LogPrint("spork", "CSporkManager::GetSporkValue -- Unknown Spork ID %d\n", nSporkID);
    return -1;
}

int CSporkManager::GetSporkIDByName(std::string strName)
{
    if (strName == "SPORK_2_INSTANTSEND_ENABLED")               return SPORK_2_INSTANTSEND_ENABLED;
    if (strName == "SPORK_3_INSTANTSEND_BLOCK_FILTERING")       return SPORK_3_INSTANTSEND_BLOCK_FILTERING;
    if (strName == "SPORK_5_INSTANTSEND_MAX_VALUE")             return SPORK_5_INSTANTSEND_MAX_VALUE;
    if (strName == "SPORK_8_SMARTNODE_PAYMENT_ENFORCEMENT")     return SPORK_8_SMARTNODE_PAYMENT_ENFORCEMENT;
    if (strName == "SPORK_10_SMARTNODE_PAY_UPDATED_NODES")      return SPORK_10_SMARTNODE_PAY_UPDATED_NODES;
    if (strName == "SPORK_15_SMARTREWARDS_BLOCKS_ENABLED")      return SPORK_15_SMARTREWARDS_BLOCKS_ENABLED;
    if (strName == "SPORK_16_MINING_SIGNATURE_ENFORCEMENT")     return SPORK_16_MINING_SIGNATURE_ENFORCEMENT;
    if (strName == "SPORK_17_MINING_SIGNATURE_PUBKEYS_ENABLED") return SPORK_17_MINING_SIGNATURE_PUBKEYS_ENABLED;
    if (strName == "SPORK_18_PAY_OUTREACH2")                    return SPORK_18_PAY_OUTREACH2;
    if (strName == "SPORK_19_PAY_WEB")                          return SPORK_19_PAY_WEB;
    if (strName == "SPORK_20_PAY_QUALITY")                      return SPORK_20_PAY_QUALITY;
    if (strName == "SPORK_21_SMARTNODE_PROTOCOL_REQUIREMENT")   return SPORK_21_SMARTNODE_PROTOCOL_REQUIREMENT;

    LogPrint("spork", "CSporkManager::GetSporkIDByName -- Unknown Spork name '%s'\n", strName);
    return -1;
}

std::string CSporkManager::GetSporkNameByID(int nSporkID)
{
    switch (nSporkID) {
        case SPORK_2_INSTANTSEND_ENABLED:               return "SPORK_2_INSTANTSEND_ENABLED";
        case SPORK_3_INSTANTSEND_BLOCK_FILTERING:       return "SPORK_3_INSTANTSEND_BLOCK_FILTERING";
        case SPORK_5_INSTANTSEND_MAX_VALUE:             return "SPORK_5_INSTANTSEND_MAX_VALUE";
        case SPORK_8_SMARTNODE_PAYMENT_ENFORCEMENT:     return "SPORK_8_SMARTNODE_PAYMENT_ENFORCEMENT";
        case SPORK_10_SMARTNODE_PAY_UPDATED_NODES:      return "SPORK_10_SMARTNODE_PAY_UPDATED_NODES";
        case SPORK_15_SMARTREWARDS_BLOCKS_ENABLED:      return "SPORK_15_SMARTREWARDS_BLOCKS_ENABLED";
        case SPORK_16_MINING_SIGNATURE_ENFORCEMENT:     return "SPORK_16_MINING_SIGNATURE_ENFORCEMENT";
        case SPORK_17_MINING_SIGNATURE_PUBKEYS_ENABLED: return "SPORK_17_MINING_SIGNATURE_PUBKEYS_ENABLED";
        case SPORK_18_PAY_OUTREACH2:                    return "SPORK_18_PAY_OUTREACH2";
        case SPORK_19_PAY_WEB:                          return "SPORK_19_PAY_WEB";
        case SPORK_20_PAY_QUALITY:                      return "SPORK_20_PAY_QUALITY";
        case SPORK_21_SMARTNODE_PROTOCOL_REQUIREMENT:   return "SPORK_21_SMARTNODE_PROTOCOL_REQUIREMENT";
        default:
            LogPrint("spork", "CSporkManager::GetSporkNameByID -- Unknown Spork ID %d\n", nSporkID);
            return "Unknown";
    }
}

bool CSporkManager::SetSporkAddress(const std::string& strAddress) {
    CBitcoinAddress address(strAddress);
    if (!address.IsValid() || !address.GetKeyID(sporkPubKeyID)) {
        LogPrintf("CSporkManager::SetSporkAddress -- Failed to parse spork address\n");
        return false;
    }
    return true;
}

bool CSporkManager::SetPrivKey(std::string strPrivKey)
{
    CKey key;
    CPubKey pubKey;
    if(!CMessageSigner::GetKeysFromSecret(strPrivKey, key, pubKey)) {
        LogPrintf("CSporkManager::SetPrivKey -- Failed to parse private key\n");
        return false;
    }

    if (pubKey.GetID() != sporkPubKeyID) {
        LogPrintf("CSporkManager::SetPrivKey -- New private key does not belong to spork address\n");
        return false;
    }

    CSporkMessage spork;

    if (spork.Sign(key)) {
        // Test signing successful, proceed
        LogPrintf("CSporkManager::SetPrivKey -- Successfully initialized as spork signer\n");

        sporkPrivKey = key;
        return true;
    } else {
        LogPrintf("CSporkManager::SetPrivKey -- Test signing failed\n");
        return false;
    }

}

bool CSporkMessage::Sign(const CKey& key)
{
    if (!key.IsValid()) {
        LogPrintf("CSporkMessage::Sign -- tried to sign with invalid sporkkey.\n");
        return false;
     }

    CKeyID pubKeyId = key.GetPubKey().GetID();
    std::string strError = "";
    std::string strMessage = boost::lexical_cast<std::string>(nSporkID) + boost::lexical_cast<std::string>(nValue) + boost::lexical_cast<std::string>(nTimeSigned);

    if(!CMessageSigner::SignMessage(strMessage, vchSig, key)) {
        LogPrintf("CSporkMessage::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!CMessageSigner::VerifyMessage(pubKeyId, vchSig, strMessage, strError)) {
        LogPrintf("CSporkMessage::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}
bool CSporkMessage::CheckSignature(const CKeyID& pubKeyId) const
{
    //note: need to investigate why this is failing
    std::string strError = "";
    std::string strMessage = boost::lexical_cast<std::string>(nSporkID) + boost::lexical_cast<std::string>(nValue) + boost::lexical_cast<std::string>(nTimeSigned);

    if(!CMessageSigner::VerifyMessage(pubKeyId, vchSig, strMessage, strError)) {
        LogPrintf("CSporkMessage::CheckSignature -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

void CSporkMessage::Relay(CConnman& connman)
{
    CInv inv(MSG_SPORK, GetHash());
    connman.RelayInv(inv);
}
