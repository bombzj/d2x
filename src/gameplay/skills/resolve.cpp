#include "spear_spec.hpp"
#include "amazon_magic_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "rule_spec.hpp"
#include "bone_spec.hpp"
#include "bow_spec.hpp"
#include "damage_curve.hpp"
#include "rank_bonus.hpp"
#include "resolve.hpp"
#include "curse_resolve.hpp"
#include "projectile_path.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
float missileSpeed(int base, int perLevel, int rank) {
    const auto velocity=missileVelocityFixed(base,perLevel,rank);
    if (!velocity) throw std::runtime_error("Unsupported original missile velocity");
    return float(*velocity)*25.f/4096.f;
}
}
SkillCastSpec resolveSkill(const SkillRuleSpec &spec, const SkillEvaluationInput &input) {
    const int rank = input.rank;
    const auto &learned = input.synergyRanks;
    const int fireMasteryPercent = input.fireMasteryPercent;
    const int lightningMasteryPercent = input.lightningMasteryPercent;
    const int coldDamagePercent = input.coldDamagePercent;
    if (spec.effect == SkillBehavior::None || spec.sourceId < 0 || rank < 1 || rank > 255 ||
        spec.manaShift < 0 || spec.manaShift > 15 || spec.hitShift < 0 || spec.hitShift > 15)
        throw std::runtime_error("Unsupported original skill rank or shift");
    SkillCastSpec result;
    result.effect = spec.effect;
    result.rank = rank;
    if (spec.amazonMagic) {
        auto program = std::make_shared<AmazonMagicSpec>(*spec.amazonMagic);
        program->radius += (rank - 1) * program->radiusPerLevel;
        program->frames += (rank - 1) * program->framesPerLevel;
        program->defenseReduction += int(skillLevelBonus(rank, program->defensePerLevel));
        program->slowPercent += (rank - 1) * program->slowPerLevel;
        result.amazonMagic = std::move(program);
    }
    if (spec.bone) {
        auto program = std::make_shared<BoneSkillSpec>(*spec.bone);
        program->radius += (rank - 1) * program->radiusPerLevel;
        program->lifePercent += (rank - 1) * program->lifePerLevel;
        for (const auto &[id, percent] : program->lifeSynergies)
            if (auto found = learned.find(id); found != learned.end()) program->lifePercent += found->second * percent;
        result.bone = std::move(program);
    }
    result.sourceId = spec.sourceId;result.hitClass=spec.hitClass;
    result.castMissileId = spec.castMissileId;
    result.castMissileDuration = spec.castMissileDuration;
    result.requiresShield = spec.requiresShield;
    if (spec.curse) result.curse = evaluateCurse(*spec.curse, rank);
    result.concentrationState = spec.concentrationState;
    result.concentrationFactor = spec.concentrationFactor;
    result.heaven = spec.heaven;
    if (result.heaven) {
        auto &heaven = *result.heaven;
        heaven.limit += (rank - 1) * heaven.limitPerLevel;
        const auto synergy = learned.find(heaven.synergySkill);
        const int percent = 100 + (synergy == learned.end() ? 0 : synergy->second) * heaven.synergyPercent;
        heaven.minimum = int((int64_t(heaven.minimum) + skillLevelBonus(rank, heaven.minimumPerLevel)) * 256 * percent / 100);
        heaven.maximum = int((int64_t(heaven.maximum) + skillLevelBonus(rank, heaven.maximumPerLevel)) * 256 * percent / 100);
        heaven.healingMinimum += (rank - 1) * heaven.healingMinimumPerLevel;
        heaven.healingMaximum += (rank - 1) * heaven.healingMaximumPerLevel;
    }
    if (spec.holyShield) {
        CombatEffectSpec shield;
        shield.state = spec.state;
        shield.source = {CombatEffectSource::Skill, {}, spec.sourceId, rank};
        shield.duration = EffectFrame(spec.armorParameters[0] + int64_t(rank - 1) * spec.armorParameters[1]);
        const auto synergy = learned.find(spec.armorSynergySkills.front());
        auto &combat = shield.modifiers.combat;
        combat.shieldDefensePercent = spec.armorParameters[2] + (rank - 1) * spec.armorParameters[3] +
            (synergy == learned.end() ? 0 : synergy->second) * spec.armorParameters[7];
        combat.blockBonus = std::min(spec.armorParameters[5], spec.armorParameters[4] +
            (spec.armorParameters[5] - spec.armorParameters[4]) * (110 * rank / (rank + 6)) / 100);
        combat.smiteMinimum = spec.minimumDamage + int(skillLevelBonus(rank, spec.minimumPerLevel));
        combat.smiteMaximum = spec.maximumDamage + int(skillLevelBonus(rank, spec.maximumPerLevel));
        result.appliedEffect = std::move(shield);
    }
    if (spec.effect == SkillBehavior::HolyBolt) {
        const auto synergy = learned.find(spec.healingSynergySkill);
        const int percent = 100 + (synergy == learned.end() ? 0 : synergy->second) * spec.healingSynergyPercent;
        result.healingMinimum = float((spec.healingParameters[0] + (rank - 1) * spec.healingParameters[1]) * percent / 100);
        result.healingMaximum = float((spec.healingParameters[2] + (rank - 1) * spec.healingParameters[3]) * percent / 100);
    }
    result.delayFrames = spec.delayFrames;
    result.blizzard = spec.blizzard;
    result.telekinesisRange = spec.telekinesisRange;
    result.telekinesisKnockbackChance = spec.telekinesisKnockbackChance;
    if (spec.hydraDuration) {
        result.hydraFrames = skillRankBonus(*spec.hydraDuration,rank);
        result.hydraLimit = spec.hydraLimit;
    }
    if (spec.effect == SkillBehavior::ThunderStorm) {
        const auto &parameters = spec.stormParameters;
        const int diminished = skillDiminishingBonus({parameters[2],parameters[3]},rank);
        result.stormPeriod = std::max(5, (100 - diminished) * parameters[1] / 100 + parameters[0]);
        result.stormRadius = parameters[4];
    }
    if (spec.linearDuration) {
        const auto [base, perLevel] = *spec.linearDuration;
        CombatEffectSpec effect;
        effect.state = spec.state;
        effect.source = {CombatEffectSource::Skill, {}, spec.sourceId, rank};
        effect.duration = EffectFrame(base + int64_t(rank - 1) * perLevel);
        effect.visual.overlayId = spec.stateCue.id;
        result.appliedEffect = std::move(effect);
    }
    if (spec.shieldMaximum > 0) {
        result.shieldPercent = std::min(spec.shieldMaximum, spec.minimumDamage + int(skillLevelBonus(rank, spec.minimumPerLevel)));
        const auto synergy = learned.find(spec.shieldSynergySkill);
        result.shieldManaFactor = std::max(1, spec.shieldManaFactor - (synergy == learned.end() ? 0 : synergy->second));
    }
    if (spec.diminishingDuration) {
        const auto [minimum, maximum] = *spec.diminishingDuration;
        CombatEffectSpec effect;
        effect.state = spec.state;
        effect.source = {CombatEffectSource::Skill, {}, spec.sourceId, rank};
        effect.duration = EffectFrame(skillDiminishingBonus({minimum,maximum},rank));
        result.appliedEffect = std::move(effect);
    }
    if (spec.freezingArea) {
        const auto &program = *spec.freezingArea;
        int synergyRank = 0;
        if (auto found = learned.find(program.synergySkill); found != learned.end()) synergyRank = found->second;
        const int64_t frames = (int64_t(program.freezeFrames) + int64_t(rank - 1) * program.freezeFramesPerLevel) *
                              (100 + int64_t(synergyRank) * program.synergyPercent) / 100;
        if (frames < 0 || frames > std::numeric_limits<int>::max())
            throw std::runtime_error("Original freeze duration exceeds supported range");
        result.freezingArea = FreezingAreaCastSpec{
            program.radiusOverride > 0 ? program.radiusOverride :
                std::max(1, program.radius + (rank - 1) * program.radiusPerLevel),
            program.freezeOverride > 0 ? program.freezeOverride : int(frames)};
    }
    if (spec.effect == SkillBehavior::FrozenArmor || spec.effect == SkillBehavior::ShiverArmor ||
        spec.effect == SkillBehavior::ChillingArmor) {
        int synergyRanks = 0;
        for (int id : spec.armorSynergySkills)
            if (auto found = learned.find(id); found != learned.end()) synergyRanks += found->second;
        const auto &parameters = spec.armorParameters;
        CombatEffectSpec armor;
        armor.state = spec.state;
        armor.source = {CombatEffectSource::Skill, {}, spec.sourceId, rank};
        armor.stacking = EffectStacking::ReplaceState;
        const int64_t frames = int64_t(parameters[2]) + int64_t(rank - 1) * parameters[3] +
                               int64_t(synergyRanks) * parameters[6];
        if (frames <= 0) throw std::runtime_error("Invalid skill state duration");
        armor.duration = EffectFrame(frames);
        armor.modifiers.combat.defensePercent = parameters[0] + (rank - 1) * parameters[1];
        armor.visual.overlayId = spec.stateCue.id;
        if (spec.effect == SkillBehavior::FrozenArmor) {
            const float freeze = float((parameters[4] + (rank - 1) * parameters[5]) *
                (100 + synergyRanks * parameters[7]) / 100) / 25.f;
            armor.reactions.push_back({CombatEffectEvent::DamagedInMelee,
                FreezeAttacker{freeze, spec.hitCue.id, float(spec.hitCue.frames) / spec.hitCue.fps}});
        } else if (spec.effect == SkillBehavior::ShiverArmor)
            armor.reactions.push_back({CombatEffectEvent::AttackedInMelee, ColdMeleeRetaliation{}});
        else armor.reactions.push_back({CombatEffectEvent::HitByMissile, ColdMissileRetaliation{}});
        result.appliedEffect = std::move(armor);
    }
    if (spec.effect == SkillBehavior::BoneArmor) {
        int64_t synergy = 0;
        for (int id : spec.armorSynergySkills)
            if (auto found = learned.find(id); found != learned.end()) synergy += found->second;
        CombatEffectSpec armor;
        armor.state = spec.state;
        armor.source = {CombatEffectSource::Skill, {}, spec.sourceId, rank};
        armor.physicalShield = armor.physicalShieldMaximum =
            (int64_t(spec.armorParameters[0]) + int64_t(rank - 1) * spec.armorParameters[1] +
             synergy * spec.armorParameters[7]) * 256;
        result.appliedEffect = std::move(armor);
    }
    const int64_t scaledMana = std::max<int64_t>(0,skillManaCostFixed(spec.mana,spec.manaPerLevel,spec.manaShift,rank));
    const int64_t fixedMana = std::max<int64_t>(int64_t(spec.minimumMana) * 256, scaledMana);
    result.manaCost = float(fixedMana) / 256.f;
    result.startMana = float(spec.startMana);
    int64_t synergy = 0;
    for (int id : spec.synergySkills)
        if (auto found = learned.find(id); found != learned.end()) synergy += found->second;
    int64_t weightedBonus = 0;
    for (const auto &[id, percent] : spec.weightedSynergies)
        if (auto found = learned.find(id); found != learned.end()) weightedBonus += int64_t(found->second) * percent;
    const int64_t bonus = std::max<int64_t>(0, 100 + synergy * spec.synergyPercent + weightedBonus);
    const int damageMastery = spec.fireDamage ? fireMasteryPercent :
        spec.lightningDamage ? lightningMasteryPercent : spec.coldDamage ? coldDamagePercent : 0;
    // D2Common separates elemental minimum/maximum evaluation; physical curves retain their own rule.
    const auto minimum = spec.fireDamage || spec.lightningDamage || spec.coldDamage || spec.poisonDamage
        ? evaluateSkillMinimumDamage : evaluateSkillDamage;
    result.minimumDamage = minimum({spec.minimumDamage, spec.minimumPerLevel}, rank, spec.hitShift, bonus, damageMastery);
    result.maximumDamage = evaluateSkillDamage({spec.maximumDamage, spec.maximumPerLevel}, rank, spec.hitShift, bonus, damageMastery);
    result.arc = spec.arc;
    if (result.arc && spec.effect == SkillBehavior::ChainLightning)
        result.arc->count = std::max(1, skillRankBonus({spec.arc->count,spec.arc->countPerLevel},rank) / 5);
    if (result.arc && spec.weapon && spec.weapon->spear && spec.weapon->spear->kind == SpearSkillSpec::Kind::Strike)
        result.arc->count = std::max(1, skillRankBonus({spec.arc->count,spec.arc->countPerLevel},rank));
    result.meteor = spec.meteor;
    if (result.meteor) {
        auto &program = *result.meteor;
        program.radius += (rank - 1) * program.radiusPerLevel;
        program.fire.fireFrames = program.fireFrames + (rank - 1) * program.fireFramesPerLevel;
        const auto synergy = learned.find(program.fireSynergySkill);
        const int percent = 100 + (synergy == learned.end() ? 0 : synergy->second) * program.fireSynergyPercent;
        auto fireDamage = [&](int base, const std::array<int, 5> &steps) {
            return evaluateMissileDamageFixed({base,steps},rank,program.fire.hitShift,percent,fireMasteryPercent);
        };
        program.fire.minimumDamage = fireDamage(spec.meteor->fire.minimumDamage, program.fireMinimumPerLevel);
        program.fire.maximumDamage = fireDamage(spec.meteor->fire.maximumDamage, program.fireMaximumPerLevel);
        program.fire.hitShift = 0;
    }
    if (spec.effect == SkillBehavior::Enchant && result.appliedEffect) {
        auto &modifiers = result.appliedEffect->modifiers.combat;
        modifiers.fireMinimum = int(result.minimumDamage);
        modifiers.fireMaximum = int(result.maximumDamage);
        modifiers.attackRatingPercent = spec.enchantAttackRating + (rank - 1) * spec.enchantAttackRatingPerLevel;
    }
    const auto coldSynergy=learned.find(spec.coldSynergySkill);
    const int64_t coldFrames=skillElementalLength(spec.coldFrames,spec.coldFramesPerLevel,rank,
        coldSynergy==learned.end()?0:int64_t(coldSynergy->second)*spec.coldSynergyPercent);
    result.coldDuration = float(coldFrames) / 25.f;
    const int64_t poisonFrames = skillElementalLength(spec.poisonFrames,spec.poisonFramesPerLevel,rank);
    result.poisonDuration = float(poisonFrames) / 25.f;
    result.weapon = spec.weapon;
    if (result.weapon) {
        result.delayFrames=std::max(result.delayFrames,result.weapon->delayFrames);
        if (result.weapon->spear) {
            auto program = std::make_shared<SpearSkillSpec>(*result.weapon->spear);
            program->conversionPercent = std::clamp(program->conversionPercent + (rank - 1) * program->conversionPerLevel, 0, 100);
            if (program->kind == SpearSkillSpec::Kind::Impale)
                program->wearChance -= skillDiminishingBonus({program->wearMinimum,program->wearMaximum},rank);
            if (program->poisonTrail) {
                program->poisonTrail->minimum = int(result.minimumDamage * 256.f);
                program->poisonTrail->maximum = int(result.maximumDamage * 256.f);
                program->poisonTrail->poisonFrames = int(poisonFrames);
                program->poisonTrail->damageFromSkill = false;
            }
            if (program->kind == SpearSkillSpec::Kind::Fury) {
                program->countBase = skillRankBonus({program->countBase,program->countPerLevel},rank);
                program->childSpeed = missileSpeed(program->childVelocity,program->childVelocityPerLevel,rank);
                program->childLifetime = float(program->childFrames + rank * program->childRangePerLevel) / 25.f;
            }
            result.weapon->spear = std::move(program);
        }
        if (result.weapon->bow) {
            auto bow = std::make_shared<BowSkillSpec>(*result.weapon->bow);
            bow->conversionPercent = std::clamp(bow->conversionPercent + (rank - 1) * bow->conversionPerLevel, 0, 100);
            if (bow->strafe) bow->minimumShots = 2 + rank / 4;
            if (bow->immolation) {
                const auto found = learned.find(bow->fireSynergySkill);
                const int percent = 100 + (found == learned.end() ? 0 : found->second) * bow->fireSynergyPercent;
                const auto damage = [&](int base, const std::array<int, 5> &steps) {
                    return evaluateMissileDamageFixed({base,steps},rank,bow->fire.hitShift,percent,
                        bow->fireMastery?fireMasteryPercent:0);
                };
                bow->fire.minimumDamage = damage(bow->fire.minimumDamage, bow->fireMinimumPerLevel);
                bow->fire.maximumDamage = damage(bow->fire.maximumDamage, bow->fireMaximumPerLevel);
                bow->fire.hitShift = 0;
            }
            result.weapon->bow = std::move(bow);
        }
        result.weapon->attackRating += (rank - 1) * result.weapon->attackRatingPerLevel;
        result.weapon->damagePercent += std::max(0, rank - result.weapon->damageStartLevel) * result.weapon->damagePerLevel;
        result.weapon->attacks = std::min(result.weapon->attackLimit, result.weapon->attacks + rank - 1);
        result.weapon->stunFrames = std::min(250, result.weapon->stunFrames + (rank - 1) * result.weapon->stunPerLevel);
        result.weapon->conversionChance = std::min(result.weapon->conversionMaximum, result.weapon->conversionMinimum +
            (result.weapon->conversionMaximum - result.weapon->conversionMinimum) * (110 * rank / (rank + 6)) / 100);
        for (size_t element = 0; element < result.weapon->elementPercent.size(); ++element) {
            auto &percent = result.weapon->elementPercent[element];
            percent += (rank - 1) * result.weapon->elementPerLevel;
            for (const auto &[skill, bonus] : result.weapon->elementSynergies[element])
                if (const auto found = learned.find(skill); found != learned.end()) percent += found->second * bonus;
            const int mastery = element == 0 ? fireMasteryPercent : element == 1 ? coldDamagePercent : lightningMasteryPercent;
            percent += percent * mastery / 100;
        }
        for (const auto &[skill, percent] : result.weapon->damageSynergies)
            if (const auto found = learned.find(skill); found != learned.end())
                result.weapon->damagePercent += found->second * percent;
    }
    result.missileId = spec.missileId;
    result.missileCount = std::min(spec.missileCountLimit,skillRankBonus({spec.missileCount,spec.missileCountPerLevel},rank));
    if (result.weapon && result.weapon->spear && result.weapon->spear->kind == SpearSkillSpec::Kind::Charged) {
        const auto &program = *result.weapon->spear;
        result.missileCount = program.countBase + rank / program.countDivisor;
        // SrvSt06 also evaluates Calc[0] as the melee enhanced-damage percentage.
        result.weapon->damagePercent = result.missileCount;
    }
    result.missileNextDelay = float(spec.missileNextDelay) / 25.f;
    result.staticPercent = float(spec.staticPercent);
    result.staticMinDamage = float(spec.staticMinDamage) / 256.f;
    result.staticRadius = float(spec.staticRange + (rank - 1) * spec.staticRangePerLevel);
    result.castOverlayId = spec.castCue.id;
    result.hitOverlayId = spec.hitCue.id;
    if (spec.castCue.fps > 0)
        result.visualDuration = float(spec.castCue.frames) / spec.castCue.fps;
    if (spec.hitCue.fps > 0)
        result.hitOverlayDuration = float(spec.hitCue.frames) / spec.hitCue.fps;
    result.missileVelocity = missileSpeed(int(spec.missileVelocity),spec.missileVelocityPerLevel,rank);
    result.missileAcceleration = float(spec.missileAcceleration) * 25.f / 4096.f;
    result.missileMaxVelocity = float(spec.missileMaxVelocity * 256) * 25.f / 4096.f;
    result.missileLifetime = spec.missileLifetime + float(rank * spec.missileRangePerLevel) / 25.f;
    if (spec.firewall) {
        result.firewall = spec.firewall;
        auto &program = *result.firewall;
        program.makerFrames += rank * spec.firewallRangePerLevel;
        program.velocity = result.missileVelocity;
        program.minimumDamage = int(result.minimumDamage * 256.f);
        program.maximumDamage = int(result.maximumDamage * 256.f);
        program.hitShift = 0;
    }
    if (spec.frozenOrb) {
        const auto &orb = *spec.frozenOrb;
        FrozenOrbCastSpec cast;
        auto child = [rank](const FrozenOrbSpec::Child &source) {
            return FrozenOrbCastSpec::Child{source.missileId,
                source.lifetimeFrames + rank * source.rangePerLevel,
                missileSpeed(source.velocity,source.velocityPerLevel,rank)};
        };
        cast.bolt = child(orb.bolt); cast.nova = child(orb.nova);
        cast.lifetimeFrames = int(result.missileLifetime * 25.f + .5f);
        cast.emissionPeriod = orb.emissionPeriod; cast.directionStep = orb.directionStep;
        cast.burstStep = orb.burstStep;
        cast.novaTurnFrames = orb.novaTurnFrames; cast.novaTurnPeriod = orb.novaTurnPeriod;
        result.frozenOrb = cast;
    }
    if (spec.effect == SkillBehavior::Inferno)
        result.missileLifetime = float(std::max(1, (spec.flameFrames + (rank - 1) * spec.flameFramesPerLevel) / 2)) / 25.f;
    result.missileImpact = spec.missileImpact;
    if (result.missileImpact && result.missileImpact->areaMissile) {
        auto &area = *result.missileImpact->areaMissile;
        area.minimum = int(result.minimumDamage * 256.f);
        area.maximum = int(result.maximumDamage * 256.f);
    }
    if (result.missileImpact && result.missileImpact->cloudBurst) {
        auto &cloud = result.missileImpact->cloudBurst->cloud;
        if (cloud.damageFromSkill) {
            if (!spec.poisonDamage || poisonFrames <= 0)
                throw std::runtime_error("Poison cloud requires resolved poison skill damage");
            cloud.minimum = int(result.minimumDamage * 256.f);
            cloud.maximum = int(result.maximumDamage * 256.f);
            cloud.poisonFrames = int(poisonFrames);
            cloud.damageFromSkill = false;
        }
    }
    return result;
}
} // namespace d2x
