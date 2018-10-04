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
#include "txmempool.h"
#include "utilstrencodings.h"
#include "version.h"

#include <boost/algorithm/string.hpp>
#include <boost/dynamic_bitset.hpp>

#include <univalue.h>


SAPI::Result SAPI::Validation::Base::Validate(const std::string &parameter, const UniValue &value) const {
    return SAPI::Result(SAPI::Undefined,"undefined");
}

SAPI::Result SAPI::Validation::Bool::Validate(const string &parameter, const UniValue &value) const
{
    SAPI::Codes code = SAPI::Valid;
    return SAPI::Result(code, ResultMessage(code));
}

SAPI::Result SAPI::Validation::String::Validate(const string &parameter, const UniValue &value) const
{
    SAPI::Codes code = SAPI::Valid;

    if( value.get_str().empty() ){
        code = SAPI::EmptyString;
    }

    return SAPI::Result(code, ResultMessage(code));
}

SAPI::Result SAPI::Validation::HexString::Validate(const string &parameter, const UniValue &value) const
{
    SAPI::Result result = String::Validate(parameter, value);

    if( result == SAPI::Valid ){

        if( !IsHex(value.get_str()) ){
            SAPI::Codes code = SAPI::InvalidHexString;
            result = SAPI::Result(code, ResultMessage(code));
        }
    }

    return result;
}

SAPI::Result SAPI::Validation::SmartCashAddress::Validate(const std::string &parameter, const UniValue &value) const
{
    CBitcoinAddress address(value.get_str());
    uint160 hashBytes;
    int type = 0;
    SAPI::Codes code = SAPI::Valid;
    std::string message = std::string();

    if (!address.GetIndexKey(hashBytes, type)){
        code = SAPI::InvalidSmartCashAddress;
        message = parameter + " -- " + ResultMessage(code);
    }

    return SAPI::Result(code, message);
}


SAPI::Result SAPI::Validation::Int::Validate(const std::string &parameter, const UniValue &value) const
{
    int64_t val;
    std::string valStr = value.getValStr();
    SAPI::Codes code = SAPI::Valid;
    std::string message = std::string();

    if (!ParsePrechecks(valStr))
        code = SAPI::NumberParserFailed;
    else if (!ParseInt64(valStr, &val)){
        code = SAPI::IntOverflow;
    }

    if( code != SAPI::Valid ) message = parameter + " -- " + ResultMessage(code);

    return SAPI::Result(code, message);
}

SAPI::Result SAPI::Validation::IntRange::Validate(const std::string &parameter, const UniValue &value) const
{
    SAPI::Result result = Int::Validate(parameter, value);

    if( result != SAPI::Valid ) return result;

    SAPI::Codes code = SAPI::Valid;
    std::string message = std::string();

    int64_t val = (int64_t)value.get_int64();

    if( val < min || val > max ){
        code = SAPI::IntOutOfRange;
        message = parameter + " -- " + strprintf(ResultMessage(code), min, max);
    }

    return SAPI::Result(code, message);
}

SAPI::Result SAPI::Validation::UInt::Validate(const std::string &parameter, const UniValue &value) const
{
    uint64_t val;
    std::string valStr = value.getValStr();
    SAPI::Codes code = SAPI::Valid;
    std::string message = std::string();

    if (!ParsePrechecks(valStr))
        code = SAPI::NumberParserFailed;
    else if( valStr[0] == '-' )
        code = SAPI::UnsignedExpected;
    else if (!ParseUInt64(valStr, &val)){
        code = SAPI::IntOverflow;
    }

    if( code != SAPI::Valid ) message = parameter + " -- " + ResultMessage(code);

    return SAPI::Result(code, message);
}


SAPI::Result SAPI::Validation::UIntRange::Validate(const std::string &parameter, const UniValue &value) const
{
    SAPI::Result result = UInt::Validate(parameter, value);

    if( result != SAPI::Valid ) return result;

    SAPI::Codes code = SAPI::Valid;
    std::string message = std::string();

    uint64_t val = (uint64_t)value.get_int64();

    if( val < min || val > max ){
        code = SAPI::UIntOutOfRange;
        message = parameter + " -- " + strprintf(ResultMessage(code), min, max);
    }

    return SAPI::Result(code, message);
}

SAPI::Result SAPI::Validation::Double::Validate(const string &parameter, const UniValue &value) const
{
    double val;
    std::string valStr = value.getValStr();
    SAPI::Codes code = SAPI::Valid;
    std::string message = std::string();

    if (!ParsePrechecks(valStr))
        code = SAPI::NumberParserFailed;
    else if (!ParseDouble(valStr, &val)){
        code = SAPI::DoubleOverflow;
    }

    if( code != SAPI::Valid ) message = parameter + " -- " + ResultMessage(code);

    return SAPI::Result(code, message);
}

SAPI::Result SAPI::Validation::DoubleRange::Validate(const string &parameter, const UniValue &value) const
{
    SAPI::Result result = Double::Validate(parameter, value);

    if( result != SAPI::Valid ) return result;

    SAPI::Codes code = SAPI::Valid;
    std::string message = std::string();

    double val = value.get_real();

    if( val < min || val > max ){
        code = SAPI::DoubleOutOfRange;
        message = parameter + " -- " + strprintf(ResultMessage(code), min, max);
    }

    return SAPI::Result(code, message);
}

SAPI::Result SAPI::Validation::Amount::Validate(const string &parameter, const UniValue &value) const
{
    int64_t val;
    std::string valStr = value.getValStr();
    SAPI::Codes code = SAPI::Valid;
    std::string message = std::string();

    if (!ParsePrechecks(valStr))
        code = SAPI::NumberParserFailed;
    else if (!ParseFixedPoint(valStr, 8, &val)){
        code = SAPI::InvalidAmount;
    }else if(!MoneyRange(val)){
        code = SAPI::AmountOverflow;
    }

    if( code != SAPI::Valid ) message = parameter + " -- " + ResultMessage(code);

    return SAPI::Result(code, message);
}

SAPI::Result SAPI::Validation::AmountRange::Validate(const string &parameter, const UniValue &value) const
{
    SAPI::Result result = Amount::Validate(parameter, value);

    if( result != SAPI::Valid ) return result;

    SAPI::Codes code = SAPI::Valid;
    std::string message = std::string();

    CAmount val = value.get_amount();

    if( val < min || val > max ){
        code = SAPI::AmountOutOfRange;
        UniValue minVal = UniValueFromAmount(min);
        UniValue maxVal = UniValueFromAmount(max);
        message = parameter + " -- " + strprintf(ResultMessage(code), minVal.getValStr(), maxVal.getValStr());
    }

    return SAPI::Result(code, message);
}

std::string SAPI::Validation::ResultMessage(SAPI::Codes value)
{
    switch(value){
    case ParameterMissing:
        return "ParameterMissing";
    case InvalidType:
        return "InvalidType";
    case NumberParserFailed:
        return "Could not parse parameter to number.";
    case UnsignedExpected:
        return "UnsignedExpected";
    case IntOverflow:
        return "IntOverflow";
    case IntOutOfRange:
        return "Value out of the valid range: %d - %d";
    case UIntOverflow:
        return "UIntOverflow";
    case UIntOutOfRange:
        return "UIntOutOfRange";
    case InvalidSmartCashAddress:
        return "InvalidSmartCashAddress";
    case DoubleOutOfRange:
        return "Value out of the valid range: %8.8f - %8.8f";
    case InvalidAmount:
        return "InvalidAmount";
    case AmountOverflow:
        return "Amount out of max money range.";
    case AmountOutOfRange:
        return "Value out of the valid range: %s - %s";
    default:
        return "UNDEFINED";
    }
}
