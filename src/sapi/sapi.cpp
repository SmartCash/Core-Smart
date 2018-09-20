// Copyright (c) 2017 - 2018 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "validation.h"
#include "sapi.h"
#include "sapi_validation.h"
#include "streams.h"
#include "sync.h"
#include "rpc/client.h"
#include "txmempool.h"
#include "utilstrencodings.h"
#include "version.h"

#include <boost/algorithm/string.hpp>
#include <boost/dynamic_bitset.hpp>

#include <univalue.h>

using namespace std;

bool SAPI::Error(HTTPRequest* req, enum HTTPStatusCode status, const std::vector<SAPI::Result> &errors)
{
    UniValue arr(UniValue::VARR);

    for( auto error : errors ){
        arr.push_back(error.ToUniValue());
    }

    string strJSON = arr.write(1,1) + "\n";

    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(status, strJSON);
    return false;
}

bool SAPI::Error(HTTPRequest* req, enum HTTPStatusCode status, const std::string &message)
{
    return SAPI::Error(req, status, {SAPI::Result(SAPI::Undefined, message)});
}

bool SAPI::Error(HTTPRequest* req, SAPI::Codes code, const std::string &message)
{
    return SAPI::Error(req, HTTP_BAD_REQUEST, {SAPI::Result(code, message)});
}

static SAPI::Result ParameterBaseCheck(HTTPRequest* req, const UniValue &obj, const SAPI::BodyParameter &param)
{
    std::string key = param.key;
    SAPI::Codes code = SAPI::Valid;
    std::string err = std::string();

    if( !obj.exists(param.key) && !param.optional ){
        err = "Parameter missing: " + key;
        code = SAPI::ParameterMissing;
    }else if( obj.exists(param.key) ){

        if( obj[key].type() != param.validator->GetType() ){

            code = SAPI::InvalidType;
            err = "Invalid type for key: " + key;

            switch( param.validator->GetType() ){
                case UniValue::VARR:
                     err += " -- expected JSON-Array";
                    break;
                case UniValue::VBOOL:
                    err += " -- expected Bool";
                    break;
                case UniValue::VNULL:
                     err += " -- expected Null";
                    break;
                case UniValue::VNUM:
                     err += " -- expected Number";
                    break;
                case UniValue::VOBJ:
                    err += " -- expected Object";
                    break;
                case UniValue::VSTR:
                    err += " -- expected String";
                    break;
                default:
                    err = "ParameterBaseCheck: invalid type value.";
                    break;
                }
        }

    }

    if( code != SAPI::Valid ){
        return SAPI::Result(code, err);
    }

    return SAPI::Result();
}

bool ParseHashStr(const string& strHash, uint256& v)
{
    if (!IsHex(strHash) || (strHash.size() != 64))
        return false;

    v.SetHex(strHash);
    return true;
}

bool SAPICheckWarmup(HTTPRequest* req)
{
    std::string statusmessage;
    if (RPCIsInWarmup(&statusmessage))
        return SAPI::Error(req, HTTP_SERVICE_UNAVAILABLE, "Service temporarily unavailable: " + statusmessage);
    return true;
}

static bool SAPIStatus(HTTPRequest* req, const std::string& strURIPart)
{
    if (!SAPICheckWarmup(req))
        return false;

    UniValue result(UniValue::VOBJ);
    result.pushKV("status", true);

    string strJSON = result.write(1,1) + "\n";
    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(HTTP_OK, strJSON);

    return true;
}

static const struct {
    const char* prefix;
    bool (*handler)(HTTPRequest* req, const std::string& strReq);
} uri_prefixes[] = {
      {"/blockchain", SAPIBlockchain},
      {"/address", SAPIAddress},
      {"/transaction", SAPITransaction},
      {"/status", SAPIStatus},
};

bool StartSAPI()
{
    for (unsigned int i = 0; i < ARRAYLEN(uri_prefixes); i++)
        RegisterSAPIHandler(uri_prefixes[i].prefix, false, uri_prefixes[i].handler);
    return true;
}

void InterruptSAPI()
{
}

void StopSAPI()
{
    for (unsigned int i = 0; i < ARRAYLEN(uri_prefixes); i++)
        UnregisterSAPIHandler(uri_prefixes[i].prefix, false);
}

static bool SAPIValidateBody(HTTPRequest *req, const SAPI::Endpoint &endpoint, UniValue &bodyParameter)
{

    if( endpoint.bodyRoot != UniValue::VARR && endpoint.bodyRoot != UniValue::VOBJ )
        return true;

    std::string bodyStr = req->ReadBody();

    if ( bodyStr.empty() )
        return SAPI::Error(req, HTTP_BAD_REQUEST, "No body parameter object defined in the body: {...TBD...}");

    try{
        // Try to parse body string to json
        UniValue jVal;
        if (!jVal.read(std::string("[")+bodyStr+std::string("]")) ||
            !jVal.isArray() || jVal.size()!=1)
            throw runtime_error(string("Error parsing JSON:")+bodyStr);
        bodyParameter = jVal[0];
    }
    catch (UniValue& objError)
    {
            try // Nice formatting for standard-format error
            {
                int code = find_value(objError, "code").get_int();
                std::string message = find_value(objError, "message").get_str();
                return SAPI::Error(req, HTTP_BAD_REQUEST, message + " (code " + std::to_string(code) + ")");
            }
            catch (const std::runtime_error&) // raised when converting to invalid type, i.e. missing code or message
            {   // Show raw JSON object
                return SAPI::Error(req, HTTP_BAD_REQUEST, objError.write());
            }
    }
    catch (const std::exception& e)
    {
        return SAPI::Error(req, HTTP_BAD_REQUEST, "Error: " + std::string(e.what()));
    }

    if( endpoint.bodyRoot == UniValue::VOBJ && !bodyParameter.isObject() )
        return SAPI::Error(req, HTTP_BAD_REQUEST, "Parameter json is expedted to be a JSON object: {...TBD... }");
    else if( endpoint.bodyRoot == UniValue::VARR && !bodyParameter.isArray() )
        return SAPI::Error(req, HTTP_BAD_REQUEST, "Parameter json is expedted to be a JSON array: {...TBD... }");

    std::vector<SAPI::Result> results;

    for( auto param : endpoint.vecBodyParameter ){

        SAPI::Result &&result = ParameterBaseCheck(req, bodyParameter, param);

        if( result != SAPI::Codes::Valid ){

            results.push_back(result);

        }else if( bodyParameter.exists(param.key) ){

            SAPI::Result result = param.validator->Validate(param.key, bodyParameter[param.key]);

            if( result != SAPI::Valid ){
                results.push_back(result);
            }
        }
    }

    if( results.size() )
        return SAPI::Error(req,HTTP_BAD_REQUEST,results);

    return true;
}

bool SAPIExecuteEndpoint(HTTPRequest *req, const string &strURIPart, const std::vector<SAPI::Endpoint> &endpoints)
{
    if (!SAPICheckWarmup(req))
        return false;

    std::string strPath = std::string();
    UniValue bodyParameter;
    SAPI::Endpoint *matchedEndpoint = nullptr;

    for( auto endpoint : endpoints ){

        if( endpoint.method != req->GetRequestMethod() )
            continue;

        if( endpoint.path == "" && ( strURIPart == "" || strURIPart == "/" ) ){

            strPath = "";
            matchedEndpoint = &endpoint;

            break;
        }else if( endpoint.path == "" && strURIPart != "" ){
            continue;
        }else if( endpoint.path == "" ){
            continue;
        }

        auto pos = strURIPart.find(endpoint.path);

        if( pos == string::npos ){
            continue;
        }

        if( !SAPIValidateBody(req, endpoint, bodyParameter) )
            return false;

        strPath = strURIPart.substr(pos + endpoint.path.length());
        matchedEndpoint = &endpoint;

        break;
    }

    if( matchedEndpoint ) return matchedEndpoint->handler(req, strPath, bodyParameter );

    return SAPIUnknownEndpointHandler(req, strURIPart);
}

bool SAPIUnknownEndpointHandler(HTTPRequest* req, const std::string& strURIPart)
{
    return SAPI::Error(req, HTTP_NOT_FOUND, "Invalid endpoint: " + req->GetURI() + " with method: " + RequestMethodString(req->GetRequestMethod()));
}

string JsonString(const UniValue &obj)
{
    return obj.write(DEFAULT_SAPI_JSON_INDENT) + "\n";
}

void SAPIWriteReply(HTTPRequest *req, enum HTTPStatusCode status, const UniValue &obj)
{
    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(status, JsonString(obj));
}

void SAPIWriteReply(HTTPRequest *req, enum HTTPStatusCode status, const std::string &str)
{
    req->WriteHeader("Content-Type", "text/plain");
    req->WriteReply(status, str + "\n");
}

void SAPIWriteReply(HTTPRequest *req, const UniValue &obj)
{
    SAPIWriteReply(req, HTTP_OK, JsonString(obj));
}

void SAPIWriteReply(HTTPRequest *req, const std::string &str)
{
    SAPIWriteReply(req, HTTP_OK, str);
}
