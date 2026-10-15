#include "spear_spec.hpp"
#include "amazon_magic_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "rule_spec.hpp"
#include "bone_spec.hpp"
#include "bow_spec.hpp"
#include "corpse_search_spec.hpp"
#include "leap_spec.hpp"
#include "grim_ward_spec.hpp"
#include "assassin_spec.hpp"
#include "damage_curve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "rank_bonus.hpp"
#include "resolve.hpp"
#include "formula_parameters.hpp"
#include "gameplay/stats/modifier_values.hpp"
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
    const auto calculation=[&](std::string_view field) {
        const auto value=input.nativeFormulas->calculation(FormulaDomain::Skill,spec.sourceId,rank,field);
        if(!value) throw std::runtime_error("Unsupported native skill calculation: "+std::to_string(spec.sourceId)+"."+std::string(field));
        return *value;
    };
    const auto &learned = input.synergyRanks;
    const int fireMasteryPercent = input.fireMasteryPercent;
    const int lightningMasteryPercent = input.lightningMasteryPercent;
    const int coldDamagePercent = input.coldDamagePercent;
    if (spec.effect == SkillBehavior::None || spec.sourceId < 0 || rank < 1 || rank > 255 ||
        spec.manaShift < 0 || spec.manaShift > 15 || spec.hitShift < 0 || spec.hitShift > 15)
        throw std::runtime_error("Unsupported original skill rank or shift");
    SkillCastSpec result;
    result.effect = spec.effect;
    result.assassin = spec.assassin;
    result.rank = rank;
    result.warcry = spec.warcry;
    if(spec.leap) {
        auto leap=std::make_shared<LeapSpec>(*spec.leap);
        leap->range=skillDiminishingRatioBonus({leap->rangeMinimum,leap->rangeMaximum},rank);
        leap->knockbackRadius+=(rank-1)*leap->knockbackPerLevel;
        result.leap=std::move(leap);
    }
    if(spec.grimWard) {
        auto ward=std::make_shared<GrimWardSpec>(*spec.grimWard);
        ward->radius+=(rank-1)*ward->radiusPerLevel;ward->duration+=(rank-1)*ward->durationPerLevel;
        result.grimWard=std::move(ward);
    }
    if(spec.corpseSearch) {
        auto search=std::make_shared<CorpseSearchSpec>(*spec.corpseSearch);
        search->chance=skillDiminishingRatioBonus({search->minimumChance,search->maximumChance},rank);
        result.corpseSearch=std::move(search);
    }
    if (spec.amazonMagic) {
        auto program = std::make_shared<AmazonMagicSpec>(*spec.amazonMagic);
        program->radius += (rank - 1) * program->radiusPerLevel;
        program->frames += (rank - 1) * program->framesPerLevel;
        program->defenseReduction += int(skillLevelBonus(rank, program->defensePerLevel));
        program->slowPercent += (rank - 1) * program->slowPerLevel;
        if(input.nativeFormulas) {
            program->radius=calculation("aurarangecalc");program->frames=calculation("auralencalc");
            const auto key=input.nativeFormulas->auraStatKey(spec.sourceId,1);
            if(!key) throw std::runtime_error("Missing native Amazon aura stat");
            program->nativeStat=StatValue{*key,calculation("aurastatcalc1")};program->stat=key->id;
            if(program->radius<0) throw std::runtime_error("Negative native Amazon aura radius is not prepared");
        }
        result.amazonMagic = std::move(program);
    }
    if (spec.bone) {
        auto program = std::make_shared<BoneSkillSpec>(*spec.bone);
        program->radius += (rank - 1) * program->radiusPerLevel;
        program->lifePercent += (rank - 1) * program->lifePerLevel;
        for (const auto &[id, percent] : program->lifeSynergies)
            if (auto found = learned.find(id); found != learned.end()) program->lifePercent += found->second * percent;
        if(program->barrier && input.nativeFormulas) {
            auto native=std::make_shared<NativeSummonStats>();native->skill=spec.sourceId;native->rank=rank;native->setBaseStats=false;
            native->lifePercent=calculation("calc1");native->passiveAdds=input.nativeFormulas->passiveBuffStats(spec.sourceId,rank,false);
            native->auraWrites=input.nativeFormulas->auraStats(spec.sourceId,rank,6,true,false);
            native->inheritedSkills=input.nativeFormulas->summonSkills(spec.sourceId,rank);
            program->lifePercent=native->lifePercent;program->barrierNative=std::move(native);
            if(!program->prison) program->sideCount=calculation("calc2")/2;
        }
        result.bone = std::move(program);
    }
    result.sourceId = spec.sourceId;result.hitClass=spec.hitClass;
    result.stunFrames=spec.stunFrames+(rank-1)*spec.stunFramesPerLevel;
    result.castMissileId = spec.castMissileId;
    result.castMissileDuration = spec.castMissileDuration;
    result.requiresShield = spec.requiresShield;
    if (spec.curse) {
        result.curse=evaluateCurse(*spec.curse,rank);
        if(input.nativeFormulas) {
            auto &curse=*result.curse;curse.nativeValues.emplace();
            for(const auto binding:input.nativeFormulas->unitEvents(spec.sourceId)) if(binding.event>=0 && binding.function>0) curse.unitEvents.push_back(binding);
            if(curse.targetProcedure==CurseSpec::TargetProcedure::Necromancer)
                curse.primaryStat=input.nativeFormulas->auraStatKey(spec.sourceId,1);
            curse.nativeWrites=input.nativeFormulas->auraStats(spec.sourceId,rank,6,false,true);
            for(const auto &stat:curse.nativeWrites) curse.nativeValues->insert_or_assign(stat.key,stat.value);
            curse.modifiers=nativeCharacterModifiers(StatSnapshot{*curse.nativeValues},input.nativeFormulas->statRules(),curse.modifiers);
            curse.radius=calculation("aurarangecalc");curse.frames=calculation("auralencalc");
            if(spec.curse->reflectPercent || spec.curse->reflectPerLevel) curse.modifiers.combat.ironMaidenPercent=calculation("calc1");
            if(spec.curse->lifeTapPercent || spec.curse->lifeTapPerLevel) curse.modifiers.combat.lifeTapPercent=calculation("calc1");
            if(curse.radius<0) throw std::runtime_error("Negative native curse range is not prepared");
            curse.permanent=curse.permanent || !curse.frames;
        }
    }
    result.concentrationState = spec.concentrationState;
    result.concentrationFactor = spec.concentrationFactor;
    result.heaven = spec.heaven;
    if (result.heaven) {
        auto &heaven = *result.heaven;
        heaven.limit += (rank - 1) * heaven.limitPerLevel;
        const auto synergy = learned.find(heaven.synergySkill);
        const auto percent=nativeAdd(100,nativeMultiply(synergy==learned.end()?0:synergy->second,heaven.synergyPercent));
        heaven.minimum = evaluateMissileDamageFixed({heaven.minimum,heaven.minimumPerLevel},rank,heaven.damageShift,percent);
        heaven.maximum = evaluateMissileDamageFixed({heaven.maximum,heaven.maximumPerLevel},rank,heaven.damageShift,percent);
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
        combat.blockBonus = skillDiminishingRatioBonus({spec.armorParameters[4],spec.armorParameters[5]},rank);
        combat.smiteMinimum = spec.minimumDamage + int(skillLevelBonus(rank, spec.minimumPerLevel));
        combat.smiteMaximum = spec.maximumDamage + int(skillLevelBonus(rank, spec.maximumPerLevel));
        result.appliedEffect = std::move(shield);
    }
    if (spec.effect == SkillBehavior::HolyBolt) {
        const auto synergy = learned.find(spec.healingSynergySkill);
        const int percent = 100 + (synergy == learned.end() ? 0 : synergy->second) * spec.healingSynergyPercent;
        result.healingMinimum = Fixed8(nativeMultiplyDivide(skillRankBonus({spec.healingParameters[0],spec.healingParameters[1]},rank),percent,100));
        result.healingMaximum = Fixed8(nativeMultiplyDivide(skillRankBonus({spec.healingParameters[2],spec.healingParameters[3]},rank),percent,100));
        if(input.nativeFormulas) {
            result.healingMinimum=Fixed8(calculation("calc1"));
            result.healingMaximum=Fixed8(calculation("calc2"));
        }
    }
    result.delayFrames = spec.assassin && input.nativeFormulas?calculation("delay"):spec.delayFrames;
    result.blizzard = spec.blizzard;
    result.telekinesisRange = spec.telekinesisRange;
    result.telekinesisKnockbackChance = spec.telekinesisKnockbackChance;
    if (spec.hydraDuration) {
        result.hydraFrames=nativeAdd(spec.hydraDuration->first,nativeMultiply(rank-1,spec.hydraDuration->second));
        result.hydraLimit = spec.hydraLimit;
        if(input.nativeFormulas) {
            SummonCastSpec summon;summon.corpse=false;
            auto &native=summon.native.emplace();native.skill=spec.sourceId;native.rank=rank;native.setBaseStats=false;native.allowZeroLife=true;
            native.lifePercent=calculation("calc1");native.passiveAdds=input.nativeFormulas->passiveBuffStats(spec.sourceId,rank,false);
            native.auraWrites=input.nativeFormulas->auraStats(spec.sourceId,rank,6,true,false);
            native.inheritedSkills=input.nativeFormulas->summonSkills(spec.sourceId,rank);native.prePassiveSkills.emplace_back(spec.sourceId,rank);
            summon.limit=result.hydraLimit=calculation("petmax");result.summon=std::move(summon);
        }
    }
    if (spec.effect == SkillBehavior::ThunderStorm) {
        const auto &parameters = spec.stormParameters;
        const int diminished = skillDiminishingBonus({parameters[2],parameters[3]},rank);
        result.stormPeriod = std::max(5,nativeAdd(nativeMultiplyDivide(nativeSubtract(100,diminished),parameters[1],100),parameters[0]));
        result.stormRadius = parameters[4];
        if(input.nativeFormulas) {
            result.stormPeriod=std::max(5,calculation("perdelay"));
            result.stormRadius=input.nativeFormulas->skillNumber(spec.sourceId,"param7");
        }
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
        if(input.nativeFormulas) {
            result.shieldPercent=calculation("calc1");
            result.shieldManaFactor=std::max(1,calculation("calc2"));
        }
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
        const auto synergyRanks = skillRankSum(spec.armorSynergySkills, learned);
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
    if(spec.effect==SkillBehavior::Shout) {
        CombatEffectSpec shout;shout.state=spec.state;shout.stacking=EffectStacking::ReplaceState;
        const auto duration=skillRankBonus({spec.armorParameters[2],spec.armorParameters[3]},rank)+
            skillRankSum(spec.armorSynergySkills,learned)*spec.armorParameters[7];
        shout.duration=uint64_t(duration);shout.modifiers.combat.defensePercent=spec.armorParameters[0]+(rank-1)*spec.armorParameters[1];
        result.appliedEffect=std::move(shout);
    }
    if(spec.effect==SkillBehavior::BattleOrders) {
        CombatEffectSpec orders;orders.state=spec.state;orders.stacking=EffectStacking::ReplaceState;
        const auto frames=skillRankBonus({spec.armorParameters[0],spec.armorParameters[1]},rank)+
            skillRankSum(spec.armorSynergySkills,learned)*spec.armorParameters[7];
        orders.duration=uint64_t(frames);const int amount=spec.armorParameters[2]+(rank-1)*spec.armorParameters[3];
        orders.modifiers.combat.lifePercent=orders.modifiers.combat.manaPercent=orders.modifiers.staminaPercent=amount;
        result.appliedEffect=std::move(orders);
    }
    if(spec.effect==SkillBehavior::BattleCommand) {
        CombatEffectSpec command;command.state=spec.state;command.stacking=EffectStacking::ReplaceState;
        const auto frames=skillRankBonus({spec.armorParameters[0],spec.armorParameters[1]},rank)+
            skillRankSum(spec.armorSynergySkills,learned)*spec.armorParameters[7];
        command.duration=uint64_t(frames);command.modifiers.combat.allSkills=1;result.appliedEffect=std::move(command);
    }
    if(spec.effect==SkillBehavior::NativeDefensiveBuff) {
        CombatEffectSpec effect;effect.state=spec.state;result.appliedEffect=std::move(effect);
    }
    if (spec.effect == SkillBehavior::BoneArmor) {
        const auto synergy = skillRankSum(spec.armorSynergySkills, learned);
        CombatEffectSpec armor;
        armor.state = spec.state;
        armor.source = {CombatEffectSource::Skill, {}, spec.sourceId, rank};
        armor.physicalShield = armor.physicalShieldMaximum =
            (int64_t(spec.armorParameters[0]) + int64_t(rank - 1) * spec.armorParameters[1] +
             synergy * spec.armorParameters[7]) * 256;
        result.appliedEffect = std::move(armor);
    }
    auto mana=nativeChecked(skillManaCostFixed(spec.mana,spec.manaPerLevel,spec.manaShift,rank));
    if(input.nativeFormulas) {
        const auto value=input.nativeFormulas->parameter(FormulaDomain::Skill,spec.sourceId,rank,"usmc");
        if(!value) throw std::runtime_error("Unsupported native skill mana calculation");
        mana=*value;
    }
    const int64_t scaledMana = std::max<int64_t>(0,mana);
    const int64_t fixedMana = std::max<int64_t>(int64_t(spec.minimumMana) * 256, scaledMana);
    result.manaCost = Fixed8::fromRaw(nativeChecked(fixedMana));
    result.startMana = Fixed8(spec.startMana);
    const auto synergy = skillRankSum(spec.synergySkills, learned);
    const auto weightedBonus = skillSynergyBonus(spec.weightedSynergies, learned);
    const auto preparedBonus=nativeAdd(nativeAdd(100,nativeMultiply(nativeChecked(synergy),spec.synergyPercent)),nativeChecked(weightedBonus));
    const int damageMastery = spec.fireDamage ? fireMasteryPercent :
        spec.lightningDamage ? lightningMasteryPercent : spec.coldDamage ? coldDamagePercent : spec.poisonDamage ? input.poisonDamagePercent : 0;
    // D2Common separates elemental minimum/maximum evaluation; physical curves retain their own rule.
    const bool elemental = !spec.physicalDamageCurve;
    const auto bonus=[&](bool minimum) {
        if(!input.nativeFormulas) return preparedBonus;
        if(elemental && minimum && !spec.minimumPerLevel[0] &&
            evaluateSkillDamageFixed({spec.minimumDamage,spec.minimumPerLevel},rank,spec.hitShift)<=256) return int32_t{100};
        return nativeAdd(100,calculation(elemental?"edmgsympercalc":"dmgsympercalc"));
    };
    result.minimumDamage = Fixed8::fromRaw(elemental
        ? evaluateSkillMinimumDamageFixed({spec.minimumDamage,spec.minimumPerLevel},rank,spec.hitShift,bonus(true),damageMastery)
        : evaluatePhysicalSkillDamageFixed({spec.minimumDamage,spec.minimumPerLevel},rank,spec.hitShift,bonus(true)));
    result.maximumDamage = Fixed8::fromRaw(elemental
        ? evaluateSkillDamageFixed({spec.maximumDamage,spec.maximumPerLevel},rank,spec.hitShift,bonus(false),damageMastery)
        : evaluatePhysicalSkillDamageFixed({spec.maximumDamage,spec.maximumPerLevel},rank,spec.hitShift,bonus(false)));
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
    const auto lengthBonus=input.nativeFormulas?calculation("elensympercalc"):
        coldSynergy==learned.end()?0:nativeMultiply(coldSynergy->second,spec.coldSynergyPercent);
    const int64_t coldFrames=skillElementalLength(spec.coldFrames,spec.coldFramesPerLevel,rank,lengthBonus);
    result.coldDuration = float(coldFrames) / 25.f;
    result.coldDurationFrames=nativeChecked(coldFrames);
    const int64_t poisonFrames = skillElementalLength(spec.poisonFrames,spec.poisonFramesPerLevel,rank,
        spec.poisonDamage?lengthBonus:0);
    result.poisonDuration = float(poisonFrames) / 25.f;
    result.poisonDurationFrames=nativeChecked(poisonFrames);
    result.weapon = spec.weapon;
    if (result.weapon) {
        if(input.nativeFormulas && spec.assassin && spec.assassin->program==AssassinProgram::DragonClaw)
            result.weapon->damagePercent=calculation("calc1");
        result.delayFrames=std::max(result.delayFrames,result.weapon->delayFrames);
        if (result.weapon->spear) {
            auto program = std::make_shared<SpearSkillSpec>(*result.weapon->spear);
            program->conversionPercent = std::clamp(program->conversionPercent + (rank - 1) * program->conversionPerLevel, 0, 100);
            if (program->kind == SpearSkillSpec::Kind::Impale)
                program->wearChance -= skillDiminishingBonus({program->wearMinimum,program->wearMaximum},rank);
            if (program->poisonTrail) {
                program->poisonTrail->minimum = result.minimumDamage.raw();
                program->poisonTrail->maximum = result.maximumDamage.raw();
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
        result.weapon->physicalFlat += (rank - 1) * result.weapon->physicalFlatPerLevel;
        result.weapon->attackRating += int(skillSynergyBonus(result.weapon->attackRatingSynergies,learned));
        result.weapon->damagePercent += std::max(0, rank - result.weapon->damageStartLevel) * result.weapon->damagePerLevel;
        result.weapon->attacks = std::min(result.weapon->attackLimit, result.weapon->attacks + rank - 1);
        result.weapon->stunFrames = std::min(250, result.weapon->stunFrames + (rank - 1) * result.weapon->stunPerLevel);
        if(spec.weapon->stunCurve) {
            const auto synergy=skillSynergyBonus(spec.weapon->stunSynergies,learned);
            result.weapon->stunFrames=int(std::min<int64_t>(250,skillElementalLength(spec.weapon->stunFrames,*spec.weapon->stunCurve,rank,synergy)));
        }
        result.weapon->conversionChance = skillDiminishingRatioBonus({result.weapon->conversionMinimum,result.weapon->conversionMaximum},rank);
        if(spec.effect==SkillBehavior::Conversion) {
            result.weapon->conversionChance=input.nativeFormulas?calculation("calc1"):result.weapon->conversionChance;
            result.weapon->conversionFrames=std::max(1,input.nativeFormulas?calculation("auralencalc"):
                skillRankBonus({spec.weapon->conversionFrames,spec.weapon->conversionFramesPerLevel},rank));
        }
        for (size_t element = 0; element < result.weapon->elementPercent.size(); ++element) {
            auto &percent = result.weapon->elementPercent[element];
            percent += (rank - 1) * result.weapon->elementPerLevel;
            percent += int(skillSynergyBonus(result.weapon->elementSynergies[element],learned));
            const int mastery = element == 0 ? fireMasteryPercent : element == 1 ? coldDamagePercent : lightningMasteryPercent;
            percent=nativeAdd(percent,nativePercentage(percent,mastery,100));
        }
        result.weapon->damagePercent += int(skillSynergyBonus(result.weapon->damageSynergies,learned));
        result.weapon->attackDefense += (rank-1)*result.weapon->attackDefensePerLevel;
        if(result.weapon->attackDurationMaximum>0) result.weapon->attackDuration=result.weapon->attackDurationMaximum-
            skillDiminishingRatioBonus({0,result.weapon->attackDurationMaximum-result.weapon->attackDurationMinimum},rank);
        result.weapon->magicConversionPercent+=int(skillSynergyBonus(result.weapon->magicConversionSynergies,learned));
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
    result.staticPercent = spec.staticPercent;
    result.staticMinDamage = Fixed8::fromRaw(spec.staticMinDamage);
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
        program.minimumDamage = result.minimumDamage.raw();
        program.maximumDamage = result.maximumDamage.raw();
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
        area.minimum = result.minimumDamage.raw();
        area.maximum = result.maximumDamage.raw();
    }
    if (result.missileImpact && result.missileImpact->cloudBurst) {
        auto &cloud = result.missileImpact->cloudBurst->cloud;
        if (cloud.damageFromSkill) {
            if (!spec.poisonDamage || poisonFrames <= 0)
                throw std::runtime_error("Poison cloud requires resolved poison skill damage");
            cloud.minimum = result.minimumDamage.raw();
            cloud.maximum = result.maximumDamage.raw();
            cloud.poisonFrames = int(poisonFrames);
            cloud.damageFromSkill = false;
        }
    }
    const bool defensiveBuff=spec.holyShield || spec.effect==SkillBehavior::FrozenArmor || spec.effect==SkillBehavior::ShiverArmor ||
        spec.effect==SkillBehavior::ChillingArmor || spec.effect==SkillBehavior::BoneArmor || spec.effect==SkillBehavior::NativeDefensiveBuff;
    const bool nativeWarcry=spec.effect==SkillBehavior::Shout || spec.effect==SkillBehavior::BattleOrders || spec.effect==SkillBehavior::BattleCommand;
    const bool bladeShield=spec.assassin && spec.assassin->program==AssassinProgram::BladeShield;
    if(bladeShield && input.nativeFormulas) {
        CombatEffectSpec effect;effect.state=spec.assassin->state;effect.source={CombatEffectSource::Skill,{},spec.sourceId,rank};
        result.appliedEffect=std::move(effect);
    }
    const bool nativeTimedBuff=spec.effect==SkillBehavior::EnergyShield || spec.effect==SkillBehavior::Blaze || spec.effect==SkillBehavior::ThunderStorm || bladeShield;
    if(input.nativeFormulas && result.appliedEffect && (defensiveBuff || spec.effect==SkillBehavior::Enchant || nativeWarcry || nativeTimedBuff)) {
        auto &effect=*result.appliedEffect;effect.nativeValues.emplace();
        effect.stacking=nativeWarcry?EffectStacking::ReuseState:EffectStacking::SkillLevel;
        effect.nativeState=true;effect.refreshPassivesOnRemoval=true;
        effect.registersUnitEvents=defensiveBuff || nativeTimedBuff;
        for(const auto binding:input.nativeFormulas->unitEvents(spec.sourceId)) {
            if(binding.event<0) {if(defensiveBuff) break;continue;}
            if(binding.function>0) effect.unitEvents.push_back(binding);
        }
        if(nativeWarcry) {
            effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;
        }
        const auto &rules=input.nativeFormulas->statRules();
        for(const auto &stat:input.nativeFormulas->auraStats(spec.sourceId,rank,6,true)) {
            result.nativeEffectWrites.push_back(stat);
            effect.nativeValues->insert_or_assign(stat.key,stat.value);
            if(spec.effect!=SkillBehavior::Enchant && stat.key.id==rules.names.at("attackrate")) {
                effect.nativeValues->insert_or_assign(StatKey{rules.names.at("other_animrate"),0},stat.value);
                result.nativeEffectWrites.emplace_back(StatKey{rules.names.at("other_animrate"),0},stat.value);
            }
        }
        if(defensiveBuff || nativeTimedBuff) for(const auto &stat:input.nativeFormulas->passiveBuffStats(spec.sourceId,rank)) {
            result.nativeEffectWrites.push_back(stat);
            effect.nativeValues->insert_or_assign(stat.key,stat.value);
            if(stat.key.id==rules.names.at("attackrate")) {
                effect.nativeValues->insert_or_assign(StatKey{rules.names.at("other_animrate"),0},stat.value);
                result.nativeEffectWrites.emplace_back(StatKey{rules.names.at("other_animrate"),0},stat.value);
            }
        }
        if(defensiveBuff || nativeTimedBuff) {
            result.nativeEffectWrites.emplace_back(StatKey{rules.names.at("modifierlist_skill"),0},spec.sourceId);
            result.nativeEffectWrites.emplace_back(StatKey{rules.names.at("modifierlist_level"),0},rank);
        }
        effect.modifiers=nativeCharacterModifiers(StatSnapshot{*effect.nativeValues},rules,effect.modifiers);
        if(spec.holyShield) {
            effect.modifiers.combat.shieldDefensePercent=calculation("aurastatcalc2");
            const auto physical=input.nativeFormulas->physicalRange(spec.sourceId,rank,false);
            if(!physical) throw std::runtime_error("Missing native Holy Shield physical range");
            effect.modifiers.combat.smiteMinimum=physical->first>>8;effect.modifiers.combat.smiteMaximum=physical->second>>8;
        }
        if(spec.effect==SkillBehavior::BoneArmor) {
            effect.physicalShieldStat=StatKey{rules.names.at("bonearmor"),0};
            effect.physicalShieldMaximumStat=StatKey{rules.names.at("bonearmormax"),0};
            const StatSnapshot values{*effect.nativeValues};
            effect.physicalShield=values.get(*effect.physicalShieldStat);effect.physicalShieldMaximum=values.get(*effect.physicalShieldMaximumStat);
            effect.nativeValues->erase(*effect.physicalShieldStat);effect.nativeValues->erase(*effect.physicalShieldMaximumStat);
        }
        const auto duration=calculation("auralencalc");
        if(!nativeWarcry && !duration) effect.duration.reset();
        else effect.duration=EffectFrame(uint32_t(duration));
    }
    return result;
}
} // namespace d2x
