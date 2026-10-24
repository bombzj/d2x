#include "attributes.hpp"
#include "gameplay/actions/movement_timing.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/combat/unit_values.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/stats/pd2/item_values.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
MovementTiming nativeMovementTiming(int baseVelocity,int baseAnimationRate,int percentage) {
    percentage=std::max(25,percentage);
    return {nativePercentage(nativeShiftLeft(baseVelocity,8),percentage,100),
        std::clamp(nativePercentage(baseAnimationRate,percentage,100),0,32767),percentage};
}
MovementTiming playerMovementTiming(int walkVelocity,int runVelocity,
    int fasterMoveVelocity,int velocityPercent,bool running) {
    if(walkVelocity<=0 || runVelocity<=0 || fasterMoveVelocity<0)
        throw std::runtime_error("Unsupported original movement inputs");
    const int faster=fasterMoveVelocity?nativePercentage(fasterMoveVelocity,150,nativeAdd(fasterMoveVelocity,150)):0;
    int percent=nativeAdd(nativeAdd(100,faster),velocityPercent);
    if(running) percent=nativeAdd(percent,nativeAdd(nativePercentage(runVelocity,100,walkVelocity),-100));
    return nativeMovementTiming(walkVelocity,running?101:213,percent);
}
int manaRecoveryPerFrame(int32_t maximumManaFixed, int denominator, int recoveryBonus) {
    auto frames = nativeMultiply(25, denominator);
    if (!frames) frames = 7500;
    const auto base = std::max(1, nativeDivide(maximumManaFixed, frames));
    return nativePercentage(base, nativeAdd(100, recoveryBonus), 100);
}
int64_t allocatedPoints(const AttributeAllocation &a) {
    return int64_t(a.strength) + a.dexterity + a.vitality + a.energy;
}
void mergeCharacterModifiers(CharacterModifiers &a, const CharacterModifiers &b) {
    auto add = [](int &target, int value) {
        const auto sum = int64_t(target) + value;
        if (sum < std::numeric_limits<int>::min() || sum > std::numeric_limits<int>::max())
            throw std::runtime_error("Character modifier sum exceeds supported range");
        target = int(sum);
    };
    add(a.strength, b.strength); add(a.dexterity, b.dexterity);
    add(a.vitality, b.vitality); add(a.energy, b.energy);
    add(a.maxLife, b.maxLife); add(a.maxMana, b.maxMana); add(a.maxStamina, b.maxStamina);
    add(a.baseLife, b.baseLife);
    add(a.lifeAfterPercentFixed,b.lifeAfterPercentFixed);add(a.manaAfterPercentFixed,b.manaAfterPercentFixed);add(a.staminaAfterPercentFixed,b.staminaAfterPercentFixed);
    add(a.attackRating, b.attackRating); add(a.defense, b.defense);
    add(a.fireResist, b.fireResist); add(a.coldResist, b.coldResist);
    add(a.lightningResist, b.lightningResist); add(a.poisonResist, b.poisonResist);
    add(a.lightRadius, b.lightRadius);
    add(a.fasterMoveVelocity, b.fasterMoveVelocity);
    add(a.velocityPercent, b.velocityPercent);
    add(a.otherAnimationRate, b.otherAnimationRate);
    add(a.staminaDrainPercent, b.staminaDrainPercent);
    add(a.staminaRecoveryBonus, b.staminaRecoveryBonus);
    add(a.staminaPercent, b.staminaPercent);
    a.torsoSpeed = std::max(a.torsoSpeed, b.torsoSpeed);
    mergeCombatModifiers(a.combat, b.combat);
}
CharacterAttributes deriveCharacterAttributes(const CharacterDefinition &d, int level,
                                               const AttributeAllocation &a,
                                               const CharacterModifiers &input,
                                               int resistancePenalty,const StatRuleCatalog *statRules,const StatSnapshot *nativeStats) {
    if (level < 1 || a.strength < 0 || a.dexterity < 0 || a.vitality < 0 || a.energy < 0)
        throw std::runtime_error("Invalid character progression");
    CharacterAttributes result;
    auto m=input;
    const bool native=statRules && nativeStats;
    const auto stat=[&](std::string_view name){return logicalStatValue(*nativeStats,*statRules,name);};
    if(native) {
        if(statRules->pd2) m.combat.pd2=pd2ItemModifiers(*nativeStats,*statRules);
        m.fireResist=stat("fireresist");m.lightningResist=stat("lightresist");m.coldResist=stat("coldresist");m.poisonResist=stat("poisonresist");
        m.lightRadius=stat("item_lightradius");m.fasterMoveVelocity=stat("item_fastermovevelocity");m.velocityPercent=stat("velocitypercent");m.otherAnimationRate=stat("other_animrate");
        m.staminaRecoveryBonus=stat("staminarecoverybonus");m.staminaDrainPercent=stat("item_staminadrainpct");
        m.combat.fasterCast=stat("item_fastercastrate");m.combat.fasterHitRecovery=stat("item_fastergethitrate");m.combat.fasterBlock=stat("item_fasterblockrate");
        m.combat.fireMaxResist=stat("maxfireresist");m.combat.lightningMaxResist=stat("maxlightresist");m.combat.coldMaxResist=stat("maxcoldresist");m.combat.poisonMaxResist=stat("maxpoisonresist");m.combat.magicMaxResist=stat("maxmagicresist");
        m.combat.physicalResist=stat("damageresist");m.combat.magicResist=stat("magicresist");
        m.combat.flatPhysicalReduction=stat("normal_damage_reduction");m.combat.flatMagicReduction=stat("magic_damage_reduction");
        m.combat.fireAbsorb=stat("item_absorbfire");m.combat.fireAbsorbPercent=stat("item_absorbfire_percent");
        m.combat.lightningAbsorb=stat("item_absorblight");m.combat.lightningAbsorbPercent=stat("item_absorblight_percent");
        m.combat.coldAbsorb=stat("item_absorbcold");m.combat.coldAbsorbPercent=stat("item_absorbcold_percent");m.combat.magicAbsorb=stat("item_absorbmagic");m.combat.magicAbsorbPercent=stat("item_absorbmagic_percent");
        m.combat.replenishLife=stat("hpregen");m.combat.manaRecovery=stat("manarecoverybonus");
        m.combat.fireSkillDamagePercent=stat("passive_fire_mastery");m.combat.lightningSkillDamagePercent=stat("passive_ltng_mastery");
        m.combat.coldSkillDamagePercent=stat("passive_cold_mastery");m.combat.magicSkillDamagePercent=stat("passive_mag_mastery");m.combat.poisonSkillDamagePercent=stat("passive_pois_mastery");
        m.combat.firePierce=stat("passive_fire_pierce");m.combat.lightningPierce=stat("passive_ltng_pierce");m.combat.coldPierce=stat("passive_cold_pierce");m.combat.poisonPierce=stat("passive_pois_pierce");
        m.combat.criticalStrike=stat("passive_critical_strike");m.combat.dodge=stat("passive_dodge");m.combat.avoid=stat("passive_avoid");m.combat.evade=stat("passive_evade");m.combat.pierce=stat("skill_pierce");
    }
    auto bounded = [](int64_t value, int minimum = 0) {
        if (value < minimum || value > std::numeric_limits<int>::max())
            throw std::runtime_error("Character attribute exceeds supported range");
        return int(value);
    };
    result.strength = native?stat("strength"):bounded(int64_t(d.strength) + a.strength + m.strength);
    result.dexterity = native?stat("dexterity"):bounded(int64_t(d.dexterity) + a.dexterity + m.dexterity);
    result.vitality = native?stat("vitality"):bounded(int64_t(d.vitality) + a.vitality + m.vitality);
    result.energy = native?stat("energy"):bounded(int64_t(d.energy) + a.energy + m.energy);
    auto quarter = [](int base,int perLevel,int points,int perPoint,int level) {
        return nativeShiftLeft(nativeAdd(nativeAdd(nativeMultiply(base,4),nativeMultiply(level-1,perLevel)),nativeMultiply(points,perPoint)),6);
    };
    const auto naturalLife=quarter(nativeAdd(nativeAdd(d.lifeAdd,d.vitality),m.baseLife),d.lifePerLevel,a.vitality,d.lifePerVitality,level);
    const auto naturalMana=quarter(d.energy,d.manaPerLevel,a.energy,d.manaPerEnergy,level);
    const auto naturalStamina=quarter(d.stamina,d.staminaPerLevel,a.vitality,d.staminaPerVitality,level);
    const auto maximum=[](int base,int flat,int percent,int bonusPoints,int perPoint,int afterPercent) {
        // D2StatList op 8/9 adds equipment attributes; op 11 percentages
        // reference the pre-op total. Per-level op contributions are separate.
        const auto before=nativeAdd(base,nativeShiftLeft(flat,8));
        const auto scaled=nativeAdd(before,nativeStatPercentage(before,percent));
        const auto attributes=nativeShiftLeft(nativeMultiply(bonusPoints,perPoint),6);
        const auto value=nativeAdd(nativeAdd(scaled,attributes),afterPercent);
        if(value<256) throw std::runtime_error("Character resource maximum outside supported native range");
        return Fixed8::fromRaw(value);
    };
    if(native) {
        result.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(*nativeStats,*statRules,"maxhp"));
        result.maxManaFixed=Fixed8::fromRaw(nativeStatValue(*nativeStats,*statRules,"maxmana"));
        result.maxStaminaFixed=Fixed8::fromRaw(nativeStatValue(*nativeStats,*statRules,"maxstamina"));
        if(result.maxLifeFixed<=0 || result.maxManaFixed<0 || result.maxStaminaFixed<0) throw std::runtime_error("Native actor resource maximum cannot be admitted");
    } else {
        result.maxLifeFixed=maximum(naturalLife,m.maxLife,m.combat.lifePercent,m.vitality,d.lifePerVitality,m.lifeAfterPercentFixed);
        result.maxManaFixed=maximum(naturalMana,m.maxMana,m.combat.manaPercent,m.energy,d.manaPerEnergy,m.manaAfterPercentFixed);
        result.maxStaminaFixed=maximum(naturalStamina,m.maxStamina,m.staminaPercent,m.vitality,d.staminaPerVitality,m.staminaAfterPercentFixed);
    }
    result.maxLife=result.maxLifeFixed.raw() >> 8;
    result.maxMana=result.maxManaFixed.raw() >> 8;
    result.maxStamina=result.maxStaminaFixed.raw() >> 8;
    result.baseAttackRating = unitAttackRating(result.dexterity,m.attackRating,d.toHitFactor);
    result.attackRating = bounded(std::max(0,nativeAdd(result.baseAttackRating,nativePercentage(result.baseAttackRating,m.combat.attackRatingPercent,100))));
    result.defense = unitDefense(result.dexterity,m.defense,0,m.combat.defensePercent,0,m.combat.armorOverridePercent);
    auto resistance = [resistancePenalty](int value, int maximumBonus) {
        const int maximum = int(std::clamp(int64_t(75) + maximumBonus, int64_t(-100), int64_t(95)));
        return int(std::clamp(int64_t(value) + resistancePenalty, int64_t(-100), int64_t(maximum)));
    };
    result.fireResist = resistance(m.fireResist, m.combat.fireMaxResist);
    result.coldResist = resistance(m.coldResist, m.combat.coldMaxResist);
    result.lightningResist = resistance(m.lightningResist, m.combat.lightningMaxResist);
    result.poisonResist = resistance(m.poisonResist, m.combat.poisonMaxResist);
    result.uncappedColdResist=int(std::clamp<int64_t>(int64_t(m.coldResist)+resistancePenalty,INT32_MIN,INT32_MAX));
    result.uncappedPoisonResist=int(std::clamp<int64_t>(int64_t(m.poisonResist)+resistancePenalty,INT32_MIN,INT32_MAX));
    result.rawResistances=std::array{m.combat.physicalResist,m.combat.magicResist,
        nativeAdd(m.fireResist,resistancePenalty),nativeAdd(m.lightningResist,resistancePenalty),
        result.uncappedColdResist,result.uncappedPoisonResist};
    result.lightRadius = int(std::clamp(int64_t(13) + m.lightRadius,
                                        int64_t(1), int64_t(18)));
    // D2Common applies diminishing returns to item FRW, then adds the ordinary
    // velocity stat (including armor penalties and the run mode bonus).
    result.velocityPercent=m.velocityPercent;
    const auto walk=playerMovementTiming(d.walkVelocity,d.runVelocity,m.fasterMoveVelocity,m.velocityPercent,false);
    const auto run=playerMovementTiming(d.walkVelocity,d.runVelocity,m.fasterMoveVelocity,m.velocityPercent,true);
    result.walkSpeed = float(walk.velocity)*25.f/4096.f;
    result.runSpeed = float(run.velocity)*25.f/4096.f;
    result.walkAnimationRate = float(walk.animationRate)*25.f/256.f;
    result.runAnimationRate = float(run.animationRate)*25.f/256.f;
    const auto torsoMultiplier = nativeAdd(m.torsoSpeed / 10, 1);
    auto drain = nativeMultiply(nativeMultiply(2, d.runDrain), torsoMultiplier);
    drain = nativeAdd(drain, nativeMultiplyDivide(drain, m.staminaDrainPercent, -100));
    result.staminaDrainPerFrame = std::max(1, drain);
    result.staminaRecoveryBonus = m.staminaRecoveryBonus;
    result.staminaPercent = m.staminaPercent;
    result.otherAnimationRate = m.otherAnimationRate;
    result.combat = m.combat;
    result.blockFactor = d.blockFactor;
    // CharStats.ManaRegen is an engine denominator, not mana per second.
    result.manaRegenPerFrame = manaRecoveryPerFrame(result.maxManaFixed.raw(), d.manaRegen, m.combat.manaRecovery);
    return result;
}
} // namespace d2x
