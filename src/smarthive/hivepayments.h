// Copyright (c) 2018 dustinface - SmartCash Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HIVEPAYMENTS_H
#define HIVEPAYMENTS_H

#include "smarthive/hive.h"
#include "chain.h"

namespace SmartHivePayments{

typedef enum{
    Valid,
    TransactionTooEarly,
    HiveAddressMissing,
    InvalidBlockHeight
} Result;

void Init();

SmartHivePayments::Result Validate(const CTransaction& txCoinbase, int nHeight, int64_t blockTime);
void FillPayments(CMutableTransaction& txNew, int nHeight, int64_t blockTime, CAmount blockReward, std::vector<CTxOut>& voutSmartHives);

int RejectionCode(SmartHivePayments::Result result);
std::string RejectionMessage(SmartHivePayments::Result result);

}

class CSmartHiveRewardBase
{
    const CScript &script;
public:
    CSmartHiveRewardBase(SmartHive::Payee payee) :script(SmartHive::Script(payee)), payee(payee) {}
    SmartHive::Payee payee;
    void SetScript(const CScript &script);
    const CScript &GetScript() {return script;}
    virtual double GetRatio() {return 0;}
};

class CSmartHiveClassic : public CSmartHiveRewardBase
{

public:
    double ratio;
    CSmartHiveClassic(SmartHive::Payee payee, double ratio) : CSmartHiveRewardBase(payee),
                                                              ratio(ratio) {}

    double GetRatio() final { return ratio;}
};

class CSmartHiveRotation : public CSmartHiveRewardBase
{

public:
    int start;
    int end;
    CSmartHiveRotation(SmartHive::Payee payee, int start, int end) : CSmartHiveRewardBase(payee),
                                                                     start(start),
                                                                     end(end) {}
    double GetRatio() final { return (end - start + 1)/100.0;}
};


struct CSmartHiveSplit
{
    std::vector<CSmartHiveRewardBase*> hives;
    int allocation;
    double percent;

    virtual bool Valididate(const std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward) const = 0;
    virtual void FillPayment(std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartHives) const {voutSmartHives.clear();}
    CSmartHiveSplit() : hives(), allocation(0) {}
    CSmartHiveSplit(int allocation, std::vector<CSmartHiveRewardBase*> hives) : hives(hives), allocation(allocation) {
        percent = allocation / 100.0;
    }
};

struct CSmartHiveClassicSplit : public CSmartHiveSplit
{
    bool Valididate(const std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward) const final;
    void FillPayment(std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartHives) const final;
    CSmartHiveClassicSplit() : CSmartHiveSplit() {}
    CSmartHiveClassicSplit(int allocation, std::vector<CSmartHiveRewardBase*> hives) : CSmartHiveSplit(allocation, hives) {}
};

struct CSmartHiveRotationSplit : public CSmartHiveSplit
{
    bool Valididate(const std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward) const final;
    void FillPayment(std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartHives) const final;
    CSmartHiveRotationSplit() : CSmartHiveSplit() {}
    CSmartHiveRotationSplit(int allocation, std::vector<CSmartHiveRewardBase*> hives) : CSmartHiveSplit(allocation, hives) {}
};

struct CSmartHiveSplitDisabled : public CSmartHiveSplit
{
    bool Valididate(const std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward) const final {return true;}
    void FillPayment(std::vector<CTxOut> &outputs, int nHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartHives) const final {voutSmartHives.clear();}
    CSmartHiveSplitDisabled() : CSmartHiveSplit() {}
};

#endif // HIVEPAYMENTS_H
