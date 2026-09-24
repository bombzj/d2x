#pragma once
#include "core/id.hpp"
#include <map>

namespace d2x {
// Equipment, skills, monster states and timed effects contribute to the same
// derived combat snapshot. Values here are already decoded from MPQ stat IDs.
struct WeaponModifiers {
    int minimum = 0, maximum = 0;
    int enhancedDamage = 0;
    int fireMinimum = 0, fireMaximum = 0;
    int lightningMinimum = 0, lightningMaximum = 0;
    int coldMinimum = 0, coldMaximum = 0, coldFrames = 0;
    int magicMinimum = 0, magicMaximum = 0;
    int poisonMinimum = 0, poisonMaximum = 0, poisonFrames = 0, poisonSources = 0;
    int deadlyStrike = 0;
};
struct CombatModifiers {
    int damagePercent = 0, attackRatingPercent = 0;
    int minimumDamagePercent = 0, maximumDamagePercent = 0;
    int normalDamage = 0, minimumDamage = 0, maximumDamage = 0;
    int fireMinimum = 0, fireMaximum = 0;
    int lightningMinimum = 0, lightningMaximum = 0;
    int coldMinimum = 0, coldMaximum = 0, coldFrames = 0;
    int magicMinimum = 0, magicMaximum = 0;
    int poisonMinimum = 0, poisonMaximum = 0, poisonFrames = 0, poisonSources = 0;
    int fireMaxResist = 0, lightningMaxResist = 0;
    int coldMaxResist = 0, poisonMaxResist = 0, magicMaxResist = 0;
    int physicalResist = 0, magicResist = 0;
    int flatPhysicalReduction = 0, flatMagicReduction = 0;
    int fireAbsorbPercent = 0, lightningAbsorbPercent = 0;
    int coldAbsorbPercent = 0, magicAbsorbPercent = 0;
    int fireAbsorb = 0, lightningAbsorb = 0;
    int coldAbsorb = 0, magicAbsorb = 0;
    int lifePercent = 0, manaPercent = 0;
    int blockBonus = 0;
    int fasterAttack = 0, fasterCast = 0, fasterHitRecovery = 0, fasterBlock = 0;
    int lifeLeech = 0, manaLeech = 0;
    int crushingBlow = 0, openWounds = 0, deadlyStrike = 0;
    int magicFind = 0, goldFind = 0;
    int poisonLengthResist = 0;
    bool cannotBeFrozen = false, halfFreezeDuration = false;
    std::map<EntityId, WeaponModifiers> weapons;
    std::map<EntityId, int> armorPercent;
};

enum class CombatEffectSource { Skill, Monster, Shrine, Item, Environment };
// Snapshot at attack launch so projectile damage does not change in flight.
struct AttackElements {
    float fire = 0, lightning = 0, cold = 0, magic = 0;
    float poisonPerSecond = 0, poisonDuration = 0, coldDuration = 0;
    bool deadly = false;
};
struct AttackDamageRange {
    int minimum = 0, maximum = 0;
};
struct AttackElementRanges {
    AttackDamageRange fire, lightning, cold, magic;
};
AttackElementRanges attackElementRanges(const CombatModifiers &combat, EntityId weapon);
void mergeCombatModifiers(CombatModifiers &target, const CombatModifiers &source);
} // namespace d2x
