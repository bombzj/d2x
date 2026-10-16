#pragma once
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include <array>
#include <cstdint>
namespace d2x {
// Weapon contact values and procedure parameters. Missile ranges and their
// per-target rolls live in MissileDamageData, without cached HP channels.
struct WeaponSkillDamage {
    std::array<int64_t,6> channels{};
    WeaponDamage weapon;
    int level{}, physicalPercent{}, pierceChance{}, coldFrames{}, poisonFrames{};
    bool automatic{}, freeze{}, projectile{}, critical{};
    int64_t physicalMinimum{},physicalMaximum{},physicalAddition{};
    uint32_t physicalRoll{}; int sourceDamage{128};
    int conversionPercent{}; DamageType conversionElement{DamageType::Magic};
    int wearChance{},wearAmount{};
    bool smite{};
    int stunFrames{};
    int lifeLeech{},manaLeech{},physicalFlat{};
    int staminaLeech{};
    bool nativeFixedPhysical{};
    int hitClass{-1};
    int crushingBlow{},openWounds{};
    bool knockback{};
    bool clearTargetStun{};
    SkillCastSpec wearSkill{};
    struct Conversion {DamageType element;int percent;};
    std::vector<Conversion> progressiveConversions;
    int freezeFrames{};
    bool softHit{};
};
WeaponSkillDamage rollPotionDamage(const WeaponDamage &, int level, uint64_t &random);
std::array<int64_t,6> targetWeaponChannels(const WeaponSkillDamage &,bool demon,bool undead);
// Procedure parameters never allocate a unit or consume a random draw.
WeaponSkillDamage prepareWeaponSkillProcedures(const WeaponDamage &,const CombatModifiers &,const SkillCastSpec &,int level,bool projectile);
struct NativeWeaponPhysicalInput {int sourceDamage{128};int32_t initialDamage{};bool critical{true};};
WeaponSkillDamage rollNativeWeaponPhysical(const WeaponDamage &,const CombatModifiers &,const SkillCastSpec &,int level,bool demon,bool undead,uint64_t &random,NativeWeaponPhysicalInput = {});

}
