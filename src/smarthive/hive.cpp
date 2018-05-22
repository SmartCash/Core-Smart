// Copyright (c) 2018 dustinface - SmartCash Developer
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
        { SmartHive::ProjectTreasury,  new CSmartAddress("SXun9XDHLdBhG4Yd1ueZfLfRpC9kZgwT1b") },
        { SmartHive::Support,          new CSmartAddress("SW2FbVaBhU1Www855V37auQzGQd8fuLR9x") },
        { SmartHive::Development,      new CSmartAddress("SPusYr5tUdUyRXevJg7pnCc9Sm4HEzaYZF") },
        { SmartHive::Outreach,         new CSmartAddress("Siim7T5zMH3he8xxtQzhmHs4CQSuMrCV1M") },
        { SmartHive::SmartRewards,     new CSmartAddress("SU5bKb35xUV8aHG5dNarWHB3HBVjcCRjYo") },
        { SmartHive::Outreach2,        new CSmartAddress("SNxFyszmGEAa2n2kQbzw7gguHa5a4FC7Ay") },
        { SmartHive::Web,              new CSmartAddress("Sgq5c4Rznibagv1aopAfPA81jac392scvm") },
        { SmartHive::Quality,          new CSmartAddress("Sc61Gc2wivtuGd6recqVDqv4R38TcHqFS8") }
    };

    addressesTestnet = {
        { SmartHive::ProjectTreasury,  new CSmartAddress("TTpGqTr2PBeVx4vvNRJ9iTq4NwpTCbSSwy") },
        { SmartHive::Support,          new CSmartAddress("THypUznpFaDHaE7PS6yAc4pHNjC2BnWzUv") },
        { SmartHive::Development,      new CSmartAddress("TDJVZE5oCYYbJQyizU4FgB2KpnKVdebnxg") },
        { SmartHive::Outreach,         new CSmartAddress("TSziXCdaBcPk3Dt94BbTH9BZDH18K6sWsc") },
        { SmartHive::SmartRewards,     new CSmartAddress("TLn1PGAVccBBjF8JuhQmATCR8vxhmamJg8") },
        { SmartHive::Outreach2,        new CSmartAddress("TCi1wcVbkmpUiTcG277o5Y3VeD3zgtsHRD") },
        { SmartHive::Web,              new CSmartAddress("TBWBQ1rCXm16huegLWvSz5TCs5KzfoYaNB") },
        { SmartHive::Quality,          new CSmartAddress("TVuTV7d5vBKyfg5j45RnnYgdo9G3ET2t2f") }
    };

    scriptsMainnet = {
        { SmartHive::ProjectTreasury,  new CScript(std::move(addressesMainnet.at(SmartHive::ProjectTreasury)->GetScript())) }, // SmartHive treasure
        { SmartHive::Support,          new CScript(std::move(addressesMainnet.at(SmartHive::Support)->GetScript())) }, // Support hive
        { SmartHive::Development,      new CScript(std::move(addressesMainnet.at(SmartHive::Development)->GetScript())) }, // Development hive
        { SmartHive::Outreach,         new CScript(std::move(addressesMainnet.at(SmartHive::Outreach)->GetScript())) }, // Outreach hive
        { SmartHive::SmartRewards,     new CScript(std::move(addressesMainnet.at(SmartHive::SmartRewards)->GetScript())) }, // Legacy smartrewards
        { SmartHive::Outreach2,        new CScript(std::move(addressesMainnet.at(SmartHive::Outreach2)->GetScript())) }, // New hive 1
        { SmartHive::Web,              new CScript(std::move(addressesMainnet.at(SmartHive::Web)->GetScript())) }, // New hive 2
        { SmartHive::Quality,          new CScript(std::move(addressesMainnet.at(SmartHive::Quality)->GetScript())) } // New hive 3
    };

    scriptsTestnet = {
        { SmartHive::ProjectTreasury,  new CScript(std::move(addressesTestnet.at(SmartHive::ProjectTreasury)->GetScript())) }, // SmartHive treasure
        { SmartHive::Support,          new CScript(std::move(addressesTestnet.at(SmartHive::Support)->GetScript())) }, // Support hive
        { SmartHive::Development,      new CScript(std::move(addressesTestnet.at(SmartHive::Development)->GetScript())) }, // Development hive
        { SmartHive::Outreach,         new CScript(std::move(addressesTestnet.at(SmartHive::Outreach)->GetScript())) }, // Outreach hive
        { SmartHive::SmartRewards,     new CScript(std::move(addressesTestnet.at(SmartHive::SmartRewards)->GetScript())) }, // Legacy smartrewards
        { SmartHive::Outreach2,        new CScript(std::move(addressesTestnet.at(SmartHive::Outreach2)->GetScript())) }, // New hive 1
        { SmartHive::Web,              new CScript(std::move(addressesTestnet.at(SmartHive::Web)->GetScript())) }, // New hive 2
        { SmartHive::Quality,          new CScript(std::move(addressesTestnet.at(SmartHive::Quality)->GetScript())) } // New hive 3
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
