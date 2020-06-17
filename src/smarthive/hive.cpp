// Copyright (c) 2017 - 2019 - The SmartCash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smarthive/hive.h"
#include "validation.h"

static std::map<SmartHive::Payee, const CSmartAddress*> addressesMainnet;
static std::map<SmartHive::Payee, const CScript*> scriptsMainnet;
static std::map<SmartHive::Payee, const CSmartAddress*> addressesTestnet;
static std::map<SmartHive::Payee, const CScript*> scriptsTestnet;

void SmartHive::Init()
{
    static bool init = false;
    if( init ) return;

    addressesMainnet = {
        { SmartHive::ProjectTreasury_Legacy,    new CSmartAddress("SXun9XDHLdBhG4Yd1ueZfLfRpC9kZgwT1b") },
        { SmartHive::Support_Legacy,            new CSmartAddress("SW2FbVaBhU1Www855V37auQzGQd8fuLR9x") },
        { SmartHive::Development_Legacy,        new CSmartAddress("SPusYr5tUdUyRXevJg7pnCc9Sm4HEzaYZF") },
        { SmartHive::Outreach_Legacy,           new CSmartAddress("Siim7T5zMH3he8xxtQzhmHs4CQSuMrCV1M") },
        { SmartHive::SmartRewards_Legacy,       new CSmartAddress("SU5bKb35xUV8aHG5dNarWHB3HBVjcCRjYo") },
        { SmartHive::Outreach2_Legacy,          new CSmartAddress("SNxFyszmGEAa2n2kQbzw7gguHa5a4FC7Ay") },
        { SmartHive::Web_Legacy,                new CSmartAddress("Sgq5c4Rznibagv1aopAfPA81jac392scvm") },
        { SmartHive::Quality_Legacy,            new CSmartAddress("Sc61Gc2wivtuGd6recqVDqv4R38TcHqFS8") },

        { SmartHive::Support,                   new CSmartAddress("TBD") },
        { SmartHive::Development,               new CSmartAddress("TBD") },
        { SmartHive::Outreach,                  new CSmartAddress("TBD") },
        { SmartHive::SmartHub,        new CSmartAddress("TBD") },
    };

    addressesTestnet = {
        { SmartHive::ProjectTreasury_Legacy,    new CSmartAddress("TTpGqTr2PBeVx4vvNRJ9iTq4NwpTCbSSwy") },
        { SmartHive::Support_Legacy,            new CSmartAddress("THypUznpFaDHaE7PS6yAc4pHNjC2BnWzUv") },
        { SmartHive::Development_Legacy,        new CSmartAddress("TDJVZE5oCYYbJQyizU4FgB2KpnKVdebnxg") },
        { SmartHive::Outreach_Legacy,           new CSmartAddress("TSziXCdaBcPk3Dt94BbTH9BZDH18K6sWsc") },
        { SmartHive::SmartRewards_Legacy,       new CSmartAddress("TLn1PGAVccBBjF8JuhQmATCR8vxhmamJg8") },
        { SmartHive::Outreach2_Legacy,          new CSmartAddress("TCi1wcVbkmpUiTcG277o5Y3VeD3zgtsHRD") },
        { SmartHive::Web_Legacy,                new CSmartAddress("TBWBQ1rCXm16huegLWvSz5TCs5KzfoYaNB") },
        { SmartHive::Quality_Legacy,            new CSmartAddress("TVuTV7d5vBKyfg5j45RnnYgdo9G3ET2t2f") },

        { SmartHive::Support,                   new CSmartAddress("6Tr3PdsFSm3DfN2b8vQ4Eqo7LzvZ238yXt") },
        { SmartHive::Development,               new CSmartAddress("6VE4Qzox3pEXtPLYhroepY9oiMS8YAgmJ9") },
        { SmartHive::Outreach,                  new CSmartAddress("6WNuCbGoM9ZeMYdW7uXwxNV7u4mgmBKmVY") },
        { SmartHive::SmartHub,        new CSmartAddress("6bF1bs7A9eth2zuZqNQmCGB2jeap7fZnUE") },
    };

    scriptsMainnet = {
        { SmartHive::ProjectTreasury_Legacy,    new CScript(std::move(addressesMainnet.at(SmartHive::ProjectTreasury_Legacy)->GetScript())) }, // SmartHive treasury
        { SmartHive::Support_Legacy,            new CScript(std::move(addressesMainnet.at(SmartHive::Support_Legacy)->GetScript())) }, // Support hive
        { SmartHive::Development_Legacy,        new CScript(std::move(addressesMainnet.at(SmartHive::Development_Legacy)->GetScript())) }, // Development hive
        { SmartHive::Outreach_Legacy,           new CScript(std::move(addressesMainnet.at(SmartHive::Outreach_Legacy)->GetScript())) }, // Outreach hive
        { SmartHive::SmartRewards_Legacy,       new CScript(std::move(addressesMainnet.at(SmartHive::SmartRewards_Legacy)->GetScript())) }, // Legacy smartrewards
        { SmartHive::Outreach2_Legacy,          new CScript(std::move(addressesMainnet.at(SmartHive::Outreach2_Legacy)->GetScript())) }, // New hive 1
        { SmartHive::Web_Legacy,                new CScript(std::move(addressesMainnet.at(SmartHive::Web_Legacy)->GetScript())) }, // New hive 2
        { SmartHive::Quality_Legacy,            new CScript(std::move(addressesMainnet.at(SmartHive::Quality_Legacy)->GetScript())) }, // New hive 3

        { SmartHive::Support,                   new CScript(std::move(addressesMainnet.at(SmartHive::Support)->GetScript())) }, // Support hive multisig
        { SmartHive::Development,               new CScript(std::move(addressesMainnet.at(SmartHive::Development)->GetScript())) }, // Development hive multisig
        { SmartHive::Outreach,                  new CScript(std::move(addressesMainnet.at(SmartHive::Outreach)->GetScript())) }, // Outreach hive multisig
        { SmartHive::SmartHub,        new CScript(std::move(addressesMainnet.at(SmartHive::SmartHub)->GetScript())) }, // Outreach hive multisig
    };

    scriptsTestnet = {
        { SmartHive::ProjectTreasury_Legacy,    new CScript(std::move(addressesTestnet.at(SmartHive::ProjectTreasury_Legacy)->GetScript())) }, // SmartHive treasure
        { SmartHive::Support_Legacy,            new CScript(std::move(addressesTestnet.at(SmartHive::Support_Legacy)->GetScript())) }, // Support hive
        { SmartHive::Development_Legacy,        new CScript(std::move(addressesTestnet.at(SmartHive::Development_Legacy)->GetScript())) }, // Development hive
        { SmartHive::Outreach_Legacy,           new CScript(std::move(addressesTestnet.at(SmartHive::Outreach_Legacy)->GetScript())) }, // Outreach hive
        { SmartHive::SmartRewards_Legacy,       new CScript(std::move(addressesTestnet.at(SmartHive::SmartRewards_Legacy)->GetScript())) }, // Legacy smartrewards
        { SmartHive::Outreach2_Legacy,          new CScript(std::move(addressesTestnet.at(SmartHive::Outreach2_Legacy)->GetScript())) }, // New hive 1
        { SmartHive::Web_Legacy,                new CScript(std::move(addressesTestnet.at(SmartHive::Web_Legacy)->GetScript())) }, // New hive 2
        { SmartHive::Quality_Legacy,            new CScript(std::move(addressesTestnet.at(SmartHive::Quality_Legacy)->GetScript())) }, // New hive 3

        { SmartHive::Support,                   new CScript(std::move(addressesTestnet.at(SmartHive::Support)->GetScript())) }, // Support hive multisig
        { SmartHive::Development,               new CScript(std::move(addressesTestnet.at(SmartHive::Development)->GetScript())) }, // Development hive multisig
        { SmartHive::Outreach,                  new CScript(std::move(addressesTestnet.at(SmartHive::Outreach)->GetScript())) }, // Outreach hive multisig
        { SmartHive::SmartHub,        new CScript(std::move(addressesTestnet.at(SmartHive::SmartHub)->GetScript())) }, // Outreach hive multisig
    };

    init = true;
}


bool SmartHive::IsHive(const CSmartAddress &address)
{
    std::map<SmartHive::Payee, const CSmartAddress*> * ptr;

    if( MainNet() ) ptr = &addressesMainnet;
    else           ptr = &addressesTestnet;

    for (auto it = ptr->begin(); it != ptr->end(); ++it )
        if( *it->second == address ) return true;

    return false;
}

bool SmartHive::IsHive(const CScript &script)
{
    std::map<SmartHive::Payee, const CScript*> * ptr;

    if( MainNet() ) ptr = &scriptsMainnet;
    else           ptr = &scriptsTestnet;

    for (auto it = ptr->begin(); it != ptr->end(); ++it )
        if( *it->second == script ) return true;

    return false;
}

const CScript* SmartHive::ScriptPtr(SmartHive::Payee payee)
{
    std::map<SmartHive::Payee, const CScript*> * ptr;

    if( MainNet() ) ptr = &scriptsMainnet;
    else           ptr = &scriptsTestnet;

    return ptr->at(payee);
}

const CSmartAddress& SmartHive::Address(SmartHive::Payee payee)
{
    std::map<SmartHive::Payee, const CSmartAddress*> * ptr;

    if( MainNet() ) ptr = &addressesMainnet;
    else           ptr = &addressesTestnet;

    return *ptr->at(payee);
}

CSmartAddress CSmartAddress::Legacy(const CSmartAddress &address)
{
    if( address.IsValid(CChainParams::PUBKEY_ADDRESS_V2) ){
        return CSmartAddress(address.ToString(false));
    }
    if( address.IsValid(CChainParams::SCRIPT_ADDRESS_V2) ){
        return CSmartAddress(address.ToString(false));
    }

    return address;
}

CSmartAddress CSmartAddress::Legacy(const std::string &strAddress)
{
    CSmartAddress address(strAddress);

    if( address.IsValid(CChainParams::PUBKEY_ADDRESS_V2) ){
        return CSmartAddress(address.ToString(false));
    }
    if( address.IsValid(CChainParams::SCRIPT_ADDRESS_V2) ){
        return CSmartAddress(address.ToString(false));
    }

    return address;
}
