#pragma once
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include <array>
#include <cstdint>
namespace d2x {
// Native launch snapshot: fixed HP channels, explicit RNG, no world or rendering.
struct WeaponSkillDamage {
    std::array<int64_t,6> channels{};
    WeaponDamage weapon;
    int level{}, physicalPercent{}, pierceChance{}, coldFrames{}, poisonFrames{};
    bool automatic{}, freeze{}, projectile{}, critical{};
    int64_t physicalMinimum{},physicalMaximum{},physicalAddition{};
    uint32_t physicalRoll{}; int sourceDamage{128};
    int conversionPercent{}; DamageType conversionElement{DamageType::Magic};
    int wearChance{},wearAmount{};
    SkillCastSpec wearSkill{};
};
std::array<int64_t,6> targetWeaponChannels(const WeaponSkillDamage &,bool demon,bool undead);
WeaponSkillDamage rollWeaponSkillDamage(const WeaponDamage &, const CombatModifiers &,
    const SkillCastSpec &, int level, bool projectile, uint64_t &random);
}
