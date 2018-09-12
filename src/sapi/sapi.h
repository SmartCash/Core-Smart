// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCASH_SAPI_H
#define SMARTCASH_SAPI_H

#include "sapiserver.h"
#include "base58.h"
#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "validation.h"

namespace SAPI{

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
    /* /address errors */
    BlockHeightOutOfRange = 2000,
    BlockNotFound,
    BlockNotSpecified,
    /* /address errors */
    AddressNotFound = 3000,
    PageOutOfRange,
    NoDepositAvailble,
    /* /transaction errors */
    TxDecodeFailed = 4000,
    TxNotSpecified,
    TxNoValidInstantPay,
    TxRejected,
    TxMissingInputs,
    TxAlreadyInBlockchain,
    TxCantRelay,
    TxNotFound
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

    std::string ResultMessage(SAPI::Codes value);
}

struct BodyParameter{
    std::string key;
    const SAPI::Validation::Base *validator;
    BodyParameter(const std::string &key,
                      const SAPI::Validation::Base *validator) : key(key), validator(validator){}
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
    bool (*handler)(HTTPRequest* req, const std::string& strReq, const UniValue &bodyParameter);
    std::vector<SAPI::BodyParameter> vecBodyParameter;
}Endpoint;

bool Error(HTTPRequest* req, enum HTTPStatusCode status, const std::string &message);
bool Error(HTTPRequest* req, enum HTTPStatusCode status, const SAPI::Result &error);
bool Error(HTTPRequest* req, enum HTTPStatusCode status, const std::vector<SAPI::Result> &errors);
bool Error(HTTPRequest* req, SAPI::Codes code, const std::string &message);

}

extern bool getAddressFromIndex(const int &type, const uint160 &hash, std::string &address);

extern bool heightSort(std::pair<CAddressUnspentKey, CAddressUnspentValue> a,
                std::pair<CAddressUnspentKey, CAddressUnspentValue> b);

extern bool ParseHashStr(const string& strHash, uint256& v);

bool SAPICheckWarmup(HTTPRequest* req);

void SAPIWriteReply(HTTPRequest *req, const UniValue& obj);
void SAPIWriteReply(HTTPRequest *req, const std::string &str);

inline std::string JsonString(const UniValue& obj);

/** Initialize SAPI server.
 * Call this before RegisterSAPIHandler or EventBase().
 */
bool InitSAPIServer();
/** Start SAPI server.
 * This is separate from InitSAPIServer to give users race-condition-free time
 * to register their handlers between InitSAPIServer and StartSAPIServer.
 */
bool StartSAPI();
/** Interrupt SAPI server threads */
void InterruptSAPI();
/** Stop SAPI server */
void StopSAPI();

bool SAPIExecuteEndpoint(HTTPRequest* req, const std::string& strURIPart, const std::vector<SAPI::Endpoint> &endpoints );
bool SAPIUnknownEndpointHandler(HTTPRequest* req, const std::string& strURIPart);

bool SAPIBlockchain(HTTPRequest* req, const std::string& strURIPart);
bool SAPIAddress(HTTPRequest* req, const std::string& strURIPart);
bool SAPITransaction(HTTPRequest* req, const std::string& strURIPart);

#endif // SMARTCASH_SAPISERVER_H
