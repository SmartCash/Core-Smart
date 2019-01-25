// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_SAPI_H
#define SMARTCASH_SAPI_H

#include "httpserver.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include <string>
#include <stdint.h>
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/function.hpp>

class CSubNet;
class CSAPIRequestCounter;

extern CSAPIRequestCounter requestCounter;

extern UniValue UniValueFromAmount(int64_t nAmount);

namespace SAPI{

extern std::string versionSubPath;
extern std::string versionString;

struct Result;

enum Codes{
    Valid = 0,
    Undefined = 1,
    /* Parameter errors */
    ParameterMissing = 1000,
    InvalidType,
    NumberParserFailed,
    UnsignedExpected,
    IntOverflow,
    IntOutOfRange,
    UIntOverflow,
    UIntOutOfRange,
    DoubleOverflow,
    DoubleOutOfRange,
    InvalidSmartCashAddress,
    EmptyString,
    InvalidHexString,
    InvalidAmount,
    AmountOverflow,
    AmountOutOfRange,
    /* common errors */
    TimedOut = 2000,
    PageOutOfRange,
    BalanceTooLow,
    RequestRateLimitReached,
    RessourceRateLimitReached,
    AddressNotFound,
    /* block errors */
    BlockHeightOutOfRange = 3000,
    BlockNotFound,
    BlockNotSpecified,
    /* address errors */
    NoDepositAvailble = 4000,
    NoUtxosAvailble,
    /* transaction errors */
    TxDecodeFailed = 5000,
    TxNotSpecified,
    TxNoValidInstantPay,
    TxRejected,
    TxMissingInputs,
    TxAlreadyInBlockchain,
    TxCantRelay,
    TxNotFound,
    /* smartreward errors */
    RewardsDatabaseBusy = 6000,
    NoActiveRewardRound,
    NoFinishedRewardRound
};

namespace Keys{

    const std::string address = "address";
    const std::string timestampFrom = "from";
    const std::string timestampTo = "to";
    const std::string pageNumber = "pageNumber";
    const std::string pageSize = "pageSize";
    const std::string amount = "amount";
    const std::string rawtx = "data";
    const std::string instantpay = "instantpay";
    const std::string overridefees = "overrideFees";
    const std::string ascending = "ascending";
    const std::string descending = "descending";
    const std::string random = "random";
    const std::string maxInputs = "maxInputs";

}

namespace Validation{

    class Base{
        UniValue::VType type;
    public:
        Base(UniValue::VType type) : type(type) {}
        virtual SAPI::Result Validate(const std::string &parameter, const UniValue &value) const;
        UniValue::VType GetType() const { return type; }
    };

    class Bool : public Base{
    public:
        Bool() : Base(UniValue::VBOOL) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const override;
    };


    class String : public Base{
    public:
        String() : Base(UniValue::VSTR) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const override;
    };

    class HexString : public String{
    public:
        HexString() : String() {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const final;
    };

    class SmartCashAddress : public Base{
    public:
        SmartCashAddress() : Base(UniValue::VSTR) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const final;
    };

    class Int : public Base{
    public:
        Int() : Base(UniValue::VNUM) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const override;
    };

    class IntRange : public Int{
        int64_t min;
        int64_t max;
    public:
        IntRange( int64_t min, int64_t max ) : Int(), min(min), max(max) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const final;
    };

    class UInt : public Base{
    public:
        UInt() : Base(UniValue::VNUM) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const override;
    };

    class UIntRange : public UInt{
        uint64_t min;
        uint64_t max;
    public:
        UIntRange( uint64_t min, uint64_t max ) : UInt(), min(min), max(max) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const final;
    };

    class Double : public Base{
    public:
        Double() : Base(UniValue::VNUM) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const override;
    };

    class DoubleRange : public Double{
        double min;
        double max;
    public:
        DoubleRange( double min, double max ) : Double(), min(min), max(max) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const final;
    };

    class Amount : public Base{
    public:
        Amount() : Base(UniValue::VNUM) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const override;
    };

    class AmountRange : public Amount{
        CAmount min;
        CAmount max;
    public:
        AmountRange( CAmount min, CAmount max ) : Amount(), min(min), max(max) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const final;
    };

    std::string ResultMessage(SAPI::Codes value);
}

namespace Limits {

    const int64_t nRequestsPerInterval = 20;
    const int64_t nRequestIntervalMs = 5000;
    const int64_t nClientRemovalMs = 10 * 60 * 1000;

    class Client{

        CCriticalSection cs;

        double nRemainingRequests;
        int64_t nLastRequestTime;

        int64_t nThrottling;
        int64_t nRequestsLimitUnlock;
        int64_t nRessourcesLimitUnlock;

    public:

        Client() {
            nRemainingRequests = nRequestsPerInterval;
            nLastRequestTime = 0;
            nThrottling = -1;
            nRequestsLimitUnlock = -1;
            nRessourcesLimitUnlock = -1;
        }
        void Request();
        bool IsRequestLimited();
        bool IsRessourceLimited();
        bool IsLimited();
        int64_t GetRequestLockSeconds();
        int64_t GetRessourceLockSeconds();
        bool CheckAndRemove();
    };

    Client *GetClient( const CService &peer );
    void CheckAndRemove();
}

struct BodyParameter{
    std::string key;
    const SAPI::Validation::Base *validator;
    bool optional;
    BodyParameter(const std::string &key,
                  const SAPI::Validation::Base *validator, bool optional = false) : key(key),
                                                                                    validator(validator),
                                                                                    optional(optional){}
};

struct Result{
    Codes code;
    std::string message;
    Result() : code(SAPI::Valid), message(std::string()) {}
    Result(SAPI::Codes code, std::string message) : code(code), message(message) {}
    friend bool operator==(const Result& a, const Codes& b)
    {
        return (a.code == b);
    }
    friend bool operator!=(const Result& a, const Codes& b)
    {
        return !(a == b);
    }
    UniValue ToUniValue() const {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("code", code);
        obj.pushKV("message", message);
        return obj;
    }
};

typedef struct {
    std::string path;
    HTTPRequest::RequestMethod method;
    UniValue::VType bodyRoot;
    bool (*handler)(HTTPRequest* req, const std::map<std::string, std::string> &mapPathParams, const UniValue &bodyParameter);
    std::vector<SAPI::BodyParameter> vecBodyParameter;
}Endpoint;

typedef struct{
    std::string prefix;
    std::vector<Endpoint> endpoints;
}EndpointGroup;

void AddWhitelistedRange(const CSubNet &subnet);
bool IsWhitelistedRange(const CNetAddr &address);

void AddDefaultHeaders(HTTPRequest* req);

bool Error(HTTPRequest* req, HTTPStatus::Codes status, const std::string &message);
bool Error(HTTPRequest* req, HTTPStatus::Codes status, const SAPI::Result &error);
bool Error(HTTPRequest* req, HTTPStatus::Codes status, const std::vector<SAPI::Result> &errors);
bool Error(HTTPRequest* req, SAPI::Codes code, const std::string &message);

void WriteReply(HTTPRequest *req, HTTPStatus::Codes status, const UniValue &obj);
void WriteReply(HTTPRequest *req, HTTPStatus::Codes status, const std::string &str);
void WriteReply(HTTPRequest *req, const UniValue& obj);
void WriteReply(HTTPRequest *req, const std::string &str);

bool CheckWarmup(HTTPRequest* req);

SAPI::Limits::Client *GetClientLimiter(const CService &peer);

int64_t GetStartTime();

}

extern bool getAddressFromIndex(const int &type, const uint160 &hash, std::string &address);

extern bool ParseHashStr(const string& strHash, uint256& v);

inline std::string JsonString(const UniValue& obj);

/** Initialize SAPI server. */
bool InitSAPIServer();
/** Start SAPI server. */
bool StartSAPIServer();
/** Interrupt SAPI server threads */
void InterruptSAPIServer();
/** Stop SAPI server */
void StopSAPIServer();

/** Start SAPI.
 * This is separate from InitSAPIServer to give users race-condition-free time
 * to register their handlers between InitSAPIServer and StartSAPIServer.
 */
bool StartSAPI();
/** Interrupt SAPI server threads */
void InterruptSAPI();
/** Stop SAPI server */
void StopSAPI();

/** Handler for requests to a certain HTTP path */
typedef std::function<bool(HTTPRequest*, const std::map<std::string, std::string> &, const SAPI::Endpoint *)> SAPIRequestHandler;

/** SAPI request work item */
class SAPIWorkItem : public HTTPClosure
{
public:
    SAPIWorkItem(std::unique_ptr<HTTPRequest> req,
                 const std::map<std::string, std::string> &mapPathParams,
                 const SAPI::Endpoint *endpoint, const SAPIRequestHandler& func):
        req(std::move(req)), mapPathParams(mapPathParams), endpoint(endpoint), func(func)
    {
    }
    void operator()()
    {
        func(req.get(), mapPathParams, endpoint);
    }

    std::unique_ptr<HTTPRequest> req;

private:
    const std::map<std::string, std::string> mapPathParams;
    const SAPI::Endpoint *endpoint;
    SAPIRequestHandler func;
};

struct CSAPIRequestCount{
    uint64_t clients;
    uint64_t valid;
    uint64_t invalid;
    uint64_t blocked;
    CSAPIRequestCount(){ Reset(); }

    uint64_t GetTotalRequests(){
        return valid + invalid + blocked;
    }

    void Reset(){
        clients = 0;
        valid = 0;
        invalid = 0;
        blocked = 0;
    }
};

class CSAPIRequestCounter
{
    const int nSecondsPerHour = 60*60;
    const int nCountLastHours = 24;

    int nLastHour;

    uint64_t nTotalValidRequests;
    uint64_t nTotalBlockedRequests;
    uint64_t nTotalInvalidRequests;

    uint64_t nMaxRequestsPerHour;
    uint64_t nMaxClientsPerHour;

    std::set<CNetAddr> setCurrentClients;
    std::vector<CSAPIRequestCount> vecRequests;

    CCriticalSection cs_requests;

public:

    enum RequestType{
        Valid,
        Invalid,
        Blocked
    };

    CSAPIRequestCounter();

    void request(CNetAddr& address, RequestType type);

    int GetCurrentHour();

    uint64_t GetTotalValidRequests(){ return nTotalValidRequests; }
    uint64_t GetTotalInvalidRequests(){ return nTotalInvalidRequests; }
    uint64_t GetTotalBlockedRequests(){ return nTotalBlockedRequests; }

    uint64_t GetMaxRequestsPerHour(){ return nMaxRequestsPerHour; }
    uint64_t GetMaxClientsPerHour(){ return nMaxClientsPerHour; }

    UniValue ToUniValue();

};

#endif // SMARTCASH_SAPI_H
