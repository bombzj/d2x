#pragma once
#include <cstdint>

namespace d2x {
struct Pd2ItemModifiers {
    int criticalChance{},maximumDeadlyStrike{},criticalMultiplier{},deadlyMultiplier{};
    int physicalPierce{},lifeDrainCap{};
};
void mergePd2ItemModifiers(Pd2ItemModifiers &,const Pd2ItemModifiers &);
// Current core 0x3048E0 combines mastery and passive critical in one roll.
struct Pd2CriticalRoll {int kind{},percentage{};};
Pd2CriticalRoll rollPd2Critical(const Pd2ItemModifiers &,int passive,int mastery,int deadly,uint64_t &random);
}
