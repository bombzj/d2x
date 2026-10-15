#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <utility>
#include <memory>
#include "core/math.hpp"

namespace d2x {
struct AssassinSkillSpec;
struct AssassinCharge {int skill{},rank{},count{},state{-1};std::shared_ptr<const AssassinSkillSpec> program;};
int32_t assassinNextCharge(int32_t previous);
std::pair<int32_t,int32_t> assassinCobraLeech(int32_t charges,int32_t leech);
struct AssassinProgressiveStep {int32_t charge{};uint16_t function{};};
std::vector<AssassinProgressiveStep> assassinProgressiveSteps(int32_t charges,bool stack,
    const std::array<uint16_t,3> &functions);
struct AssassinKickInput {
    int32_t strength{},dexterity{},skillPercent{},itemKickDamage{};
    int32_t bootMinimum{},bootMaximum{},bootStrengthBonus{},bootDexterityBonus{};
    int32_t offWeaponDamagePercent{},offWeaponMaximumPercent{};
    bool boots{};
};
// SkillAss::sub_6FCF7CE0 + D2Common::SKILLS_CalculateKickDamage. The
// caller supplies stats sampled with weapon damage lists unmerged.
std::pair<int32_t,int32_t> assassinKickRange(const AssassinKickInput &);
// SkillAss::sub_6FCF7390, after the missile's InitLowSeed callback.
std::vector<Vec> assassinProgressiveSpiral(Vec origin,Vec target,int frames,uint64_t &random);
}
