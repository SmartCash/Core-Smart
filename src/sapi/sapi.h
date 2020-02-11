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
class CSAPIStatistics;

extern CSAPIStatistics sapiStatistics;

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
    BalanceInsufficient,
    RequestRateLimitExceeded,
    RessourceRateLimitExceeded,
    AddressNotFound,
    NoInstantPayLocksAvailble,
    /* block errors */
    BlockHeightOutOfRange = 3000,
    BlockNotFound,
    BlockNotSpecified,
    BlockHashInvalid,
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
    TxMissingTxId,
    TxMissingVout,
    TxInvalidParameter,
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
    const std::string height = "height";
    const std::string hash = "hash";
    const std::string inputs = "inputs";
    const std::string outputs = "outputs";
    const std::string locktime = "locktime";
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

    class Array : public Base{
    public:
        Array() : Base(UniValue::VARR) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const override;
    };

    class Object : public Base{
    public:
        Object() : Base(UniValue::VOBJ) {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const override;
    };

    class Outputs : public Object{
    public:
        Outputs() : Object() {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const final;
    };

    class Transaction : public Object{
    public:
        Transaction() : Object() {}
        SAPI::Result Validate(const std::string &parameter, const UniValue &value) const final;
    };

    class Transactions : public Array{
    public:
        Transactions() : Array() {}
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
    int64_t nStartTimestamp;
    uint64_t nClients;
    uint64_t nValid;
    uint64_t nInvalid;
    uint64_t nBlocked;
    CSAPIRequestCount(){ Reset(); }


    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nStartTimestamp);
        READWRITE(nClients);
        READWRITE(nValid);
        READWRITE(nInvalid);
        READWRITE(nBlocked);
    }

    uint64_t GetTotalRequests(){
        return nValid + nInvalid + nBlocked;
    }

    void Reset(){
        nStartTimestamp = 0;
        nClients = 0;
        nValid = 0;
        nInvalid = 0;
        nBlocked = 0;
    }
};

class CSAPIStatistics
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

    std::vector<int64_t> vecRestarts;

    CCriticalSection cs_requests;

public:

    enum RequestType{
        Valid,
        Invalid,
        Blocked
    };

    CSAPIStatistics();

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nLastHour);
        READWRITE(nTotalValidRequests);
        READWRITE(nTotalBlockedRequests);
        READWRITE(nTotalInvalidRequests);
        READWRITE(nMaxRequestsPerHour);
        READWRITE(nMaxClientsPerHour);
        READWRITE(setCurrentClients);
        READWRITE(vecRequests);
        READWRITE(vecRestarts);
    }

    void init();
    void request(CNetAddr& address, RequestType type);
    void reset();

    int GetCurrentHour();
    int GetCurrentStartTimestamp();

    uint64_t GetTotalValidRequests(){ return nTotalValidRequests; }
    uint64_t GetTotalInvalidRequests(){ return nTotalInvalidRequests; }
    uint64_t GetTotalBlockedRequests(){ return nTotalBlockedRequests; }

    uint64_t GetMaxRequestsPerHour(){ return nMaxRequestsPerHour; }
    uint64_t GetMaxClientsPerHour(){ return nMaxClientsPerHour; }

    UniValue ToUniValue();
    std::string ToString() const;

    // Dummies..for the flatDB.
    void CheckAndRemove(){}
    void Clear(){}
};

#endif // SMARTCASH_SAPI_H
