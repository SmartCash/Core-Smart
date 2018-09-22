// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INIT_H
#define BITCOIN_INIT_H

#include <string>
#include "serialize.h"
#include "tinyformat.h"

class CScheduler;
class CWallet;

namespace boost
{
class thread_group;
} // namespace boost

extern CWallet* pwalletMain;

void StartShutdown();
bool ShutdownRequested();
/** Interrupt threads */
void Interrupt(boost::thread_group& threadGroup);
void Shutdown();
//!Initialize the logging infrastructure
void InitLogging();
//!Parameter interaction: change current parameters depending on various rules
void InitParameterInteraction();
bool AppInit2(boost::thread_group& threadGroup, CScheduler& scheduler);
void PrepareShutdown();

/** The help message mode determines what help message to show */
enum HelpMessageMode {
    HMM_BITCOIND,
    HMM_BITCOIN_QT
};

/** Help for options shared between UI and daemon (for -help) */
std::string HelpMessage(HelpMessageMode mode);
/** Returns licensing information (for -version) */
std::string LicenseInfo();


// Used to keep track of the client and protocol version.
// If one changes the caches will become cleared on startup.
class CVersionInfo
{
private:
    int clientVersion;
    int protocolVersion;

public:
    CVersionInfo(){}
    CVersionInfo(int client, int protocol): clientVersion(client), protocolVersion(protocol) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(clientVersion);
        READWRITE(protocolVersion);
    }

    int GetClientVersion(){return clientVersion;}
    int GetProtocolVersion(){return protocolVersion;}

    friend bool operator==(const CVersionInfo& a, const CVersionInfo& b)
    {
        return (a.clientVersion == b.clientVersion && a.protocolVersion == b.protocolVersion);
    }

    friend bool operator!=(const CVersionInfo& a, const CVersionInfo& b)
    {
        return !(a == b);
    }

    std::string ToString() const{return strprintf("CVersionInfo(client: %d, protocol: %d)",clientVersion,protocolVersion);}

    // Dummies..for the flatDB.
    void CheckAndRemove(){}
    void Clear(){}
};

extern CVersionInfo versionInfo;

extern std::string strClientVersion;

#endif // BITCOIN_INIT_H
