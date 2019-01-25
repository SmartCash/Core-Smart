// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sapi/sapi.h"
#include "netbase.h"
#include "util.h"

CCriticalSection cs_clients;
static std::map<std::string, SAPI::Limits::Client*> mapClients;

static std::vector<int> vecThrottling = {
    10,60,600,3600
};

SAPI::Limits::Client *SAPI::Limits::GetClient(const CService &peer)
{
    SAPI::Limits::Client *client;
    std::string strIp = peer.ToStringIP(false);

    LOCK(cs_clients);

    auto it = mapClients.find(strIp);

    if( it == mapClients.end() ){
        client = new SAPI::Limits::Client();
        mapClients.insert(std::make_pair(strIp, client));
    }else{
        client = it->second;
    }

    return client;
}

void SAPI::Limits::CheckAndRemove()
{
    LOCK(cs_clients);

    LogPrintf("SAPI::Limits::CheckAndRemove() - Clients %d\n", mapClients.size());

    auto it = mapClients.begin();

    while( it != mapClients.end() ){
        if( it->second->CheckAndRemove() ){
            LogPrintf("SAPI::Limits::CheckAndRemove() - Remove %s\n", it->first);
            delete it->second;
            it = mapClients.erase(it);
        }else{
            ++it;
        }
    }
}

void SAPI::Limits::Client::Request()
{
    LOCK(cs);

    int64_t nTime = GetTimeMillis();
    int64_t nTimePassed = nTime - nLastRequestTime;

    IsRequestLimited();

    nRemainingRequests += (static_cast<double>(nTimePassed * nRequestsPerInterval) / nRequestIntervalMs) - 1.0;

    if( nRemainingRequests > nRequestsPerInterval )
            nRemainingRequests = nRequestsPerInterval;

    LogPrintf("nRemaining before: %f\n", nRemainingRequests);

    if(nRemainingRequests <= 0){

        if( nThrottling < static_cast<int64_t>(vecThrottling.size() - 1) )
            ++nThrottling;

        int64_t nThrottlingTime = vecThrottling.at(nThrottling);

        nRequestsLimitUnlock = nTime + nThrottlingTime * 1000;
        nRemainingRequests = nRequestIntervalMs;

    }

    LogPrintf("nRemaining after: %f, throttling %d\n", nRemainingRequests, nThrottling);

    nLastRequestTime = nTime;
}

bool SAPI::Limits::Client::IsRequestLimited()
{

    LOCK(cs);

    if( nRequestsLimitUnlock < 0 )
        return false;

    int64_t nTime = GetTimeMillis();

    if( nTime > nRequestsLimitUnlock ){
        nRequestsLimitUnlock = -1;
        nThrottling = -1;
        return false;
    }

    LogPrintf("Request limited: %.2fms\n", nRequestsLimitUnlock - nTime);

    return true;
}

bool SAPI::Limits::Client::IsRessourceLimited()
{
    return false;
}

bool SAPI::Limits::Client::IsLimited()
{
    return IsRequestLimited() || IsRessourceLimited();
}

int64_t SAPI::Limits::Client::GetRequestLockSeconds()
{
    if( nRequestsLimitUnlock < 0)
        return 0;

    return (nRequestsLimitUnlock - GetTimeMillis()) / 1000;
}

int64_t SAPI::Limits::Client::GetRessourceLockSeconds()
{
    if( nRessourcesLimitUnlock < 0)
        return 0;

    return (nRessourcesLimitUnlock - GetTimeMillis()) / 1000;
}

bool SAPI::Limits::Client::CheckAndRemove()
{
    // If the client is not limited and was not active for nClientRemovalMs
    // we want to remove it from the list.
    if( !IsLimited() && ( GetTimeMillis() - nLastRequestTime ) > nClientRemovalMs )
        return true;

    return false;
}
