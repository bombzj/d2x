#include "aura_resolve.hpp"
#include "rank_bonus.hpp"
#include "formula_parameters.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/event_frame.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2x {
int32_t nativeAuraDirectStatValue(int32_t current,int32_t bonus,std::optional<int32_t> maximum) {
    const auto value=nativeAdd(current,bonus);
    return maximum && value>=*maximum?*maximum:value;
}
uint64_t nativeAuraDurationTick(uint64_t tick,uint64_t until,int remainingPercent) {
    const auto duration=nativePercentage(nativeNarrow(uint32_t(until-tick)),remainingPercent,100);
    return tick+uint32_t(duration);
}
int32_t skillPeriodicFrame(int32_t frame, int32_t delay) {
    const int32_t period=std::max(5,delay);
    const auto rounded=nativeDivide(nativeSubtract(nativeAdd(frame,period),1),period);
    return nativeAdd(nativeMultiply(rounded,period),1);
}
int32_t skillPeriodicDuration(int32_t frame, int32_t delay) {
    // SrvDo065/066 calculate the lifetime before timed-event insertion clamps
    // the due frame. Preserve that separate signed expression at wraparound.
    return nativeAdd(nativeSubtract(skillPeriodicFrame(frame,delay),frame),1);
}
uint64_t skillPeriodicTick(uint64_t tick, int32_t delay) {
    const auto frame=nativeNarrow(uint32_t(tick));
    return nativeEventDeadline(tick,skillPeriodicFrame(frame,delay)).due;
}
namespace {
int evaluate(AuraFormula formula, const AuraSkillSpec &spec, int rank,
             const std::map<int, int> &ranks, int prayerRank) {
    auto parameter = [&](int index) { return spec.parameters[size_t(index - 1)]; };
    auto linear = [&](int first) { return skillRankBonus({parameter(first), parameter(first + 1)},rank); };
    auto diminishing = [&](int first) {
        const int low = parameter(first), high = parameter(first + 1);
        return skillDiminishingBonus({low,high},rank);
    };
    switch (formula) {
    case AuraFormula::Linear12: return linear(1);
    case AuraFormula::Linear34: return linear(3);
    case AuraFormula::Linear56: return linear(5);
    case AuraFormula::Parameter5: return parameter(5);
    case AuraFormula::One: return 1;
    case AuraFormula::HalfLinear56: return linear(5) / 2;
    case AuraFormula::Diminishing34: return diminishing(3);
    case AuraFormula::RemainingDiminishing34: return 100 - diminishing(3);
    case AuraFormula::Diminishing56: return diminishing(5);
    case AuraFormula::ElementalMinimum:
        return evaluateSkillMinimumDamageFixed(spec.minimumDamage,rank,spec.damageShift);
    case AuraFormula::PrayerMinimum:
        if (prayerRank <= 0) return 0;
        if (!spec.prayerMinimum) throw std::runtime_error("Missing prepared Prayer damage curve");
        return evaluateSkillMinimumDamageFixed(*spec.prayerMinimum,prayerRank,spec.prayerShift);
    case AuraFormula::NegativeDiminishing34: return -diminishing(3);
    case AuraFormula::NegativeDiminishing56: return -diminishing(5);
    case AuraFormula::NegativeParameter5: return -parameter(5);
    case AuraFormula::NegativeCappedLinear34: return -std::min(linear(3), 150);
    case AuraFormula::AttackRating: return spec.toHit + (rank - 1) * spec.toHitPerLevel;
    case AuraFormula::BaseRank: {
        const auto base = ranks.find(spec.skill);
        return base != ranks.end() ? base->second : 0;
    }
    }
    throw std::runtime_error("Unsupported typed aura formula");
}
void apply(AuraDefinition &result, AuraStat stat, int amount) {
    auto &m = result.modifiers;
    switch (stat) {
    case AuraStat::PoisonLength: result.harmfulDurationPercent = amount; break;
    case AuraStat::Life: result.lifePerPulse = Fixed8::fromRaw(amount); break;
    case AuraStat::LifePercent: m.combat.lifePercent = amount; break;
    case AuraStat::StaminaRecovery: m.staminaRecoveryBonus = amount; break;
    case AuraStat::StaminaPercent: m.staminaPercent = amount; break;
    case AuraStat::ManaRecovery: m.combat.manaRecovery = amount; break;
    case AuraStat::DamagePercent: m.combat.damagePercent = amount; break;
    case AuraStat::AttackRatingPercent: m.combat.attackRatingPercent = amount; break;
    case AuraStat::AttackRate: m.combat.attackRate = amount; break;
    case AuraStat::AnimationRate: m.otherAnimationRate = amount; break;
    case AuraStat::Velocity: m.velocityPercent = amount; break;
    case AuraStat::DefensePercent: m.combat.defensePercent = amount; break;
    case AuraStat::FireResist: m.fireResist = amount; break;
    case AuraStat::ColdResist: m.coldResist = amount; break;
    case AuraStat::LightningResist: m.lightningResist = amount; break;
    case AuraStat::PhysicalResist: m.combat.physicalResist = amount; break;
    case AuraStat::FireMaximum: m.combat.fireMaxResist = amount; break;
    case AuraStat::ColdMaximum: m.combat.coldMaxResist = amount; break;
    case AuraStat::LightningMaximum: m.combat.lightningMaxResist = amount; break;
    case AuraStat::Thorns: m.combat.thornsPercent = amount; break;
    case AuraStat::Concentration: m.combat.concentrationChance = amount; break;
    }
}
void ownerDamage(AuraDefinition &result) {
    auto &combat = result.ownerModifiers.combat;
    const int minimum = result.skill == 118 ? 1 : nativeMultiply(result.minimumDamage.raw(),result.elementalMultiplier) >> 8;
    const int maximum = nativeMultiply(result.maximumDamage.raw(),result.elementalMultiplier) >> 8;
    if (result.skill == 102) { combat.fireMinimum = minimum; combat.fireMaximum = maximum; }
    else if (result.skill == 114) { combat.coldMinimum = minimum; combat.coldMaximum = maximum; }
    else if (result.skill == 118) { combat.lightningMinimum = minimum; combat.lightningMaximum = maximum; }
}
} // namespace
AuraDefinition evaluateBaseAura(const AuraSkillSpec &spec, int rank,
    const std::map<int, int> &ranks, int prayerRank) {
    auto value = [&](AuraFormula formula) { return evaluate(formula, spec, rank, ranks, prayerRank); };
    AuraDefinition result;
    result.skill = spec.skill;
    result.serverDoFunction=spec.serverDoFunction;
    result.rank = rank;
    result.manaPerPulse = Fixed8::fromRaw(nativeChecked(skillManaCostFixed(spec.mana,spec.manaPerLevel,spec.manaShift,rank)));
    result.filter = spec.filter;
    result.hitClass = spec.hitClass;
    result.resultFlags = spec.resultFlags;
    result.radius = float(value(spec.radius));
    result.periodFrames = spec.period ? value(*spec.period) : spec.periodFrames;
    result.hostile = spec.skill == 66 || spec.skill == 102 || spec.skill == 114 ||
        spec.skill == 118 || spec.skill == 119 || spec.skill == 123;
    if (spec.skill == 124) {
        result.redemptionChance = value(spec.redemption[0]);
        result.redemptionLife = Fixed8(value(spec.redemption[1]));
        result.redemptionMana = Fixed8(value(spec.redemption[2]));
    }
    result.ownerState = spec.ownerState;
    result.state = spec.state;
    for (const auto &entry : spec.stats) {
        const int amount = value(entry.formula);
        if (entry.stat) apply(result, *entry.stat, amount);
    }
    if (spec.skill == 122) {
        const int linear = value(AuraFormula::Linear56);
        result.ownerDamageBonus = linear - linear / 2;
    }
    result.element = spec.element;
    if (spec.element >= 0) {
        result.minimumDamage = Fixed8::fromRaw(evaluateSkillMinimumDamageFixed(spec.minimumDamage,rank,spec.damageShift));
        result.maximumDamage = Fixed8::fromRaw(evaluateSkillDamageFixed(spec.maximumDamage,rank,spec.damageShift));
        result.elementalMultiplier = spec.parameters[4];
        if (spec.skill == 119) result.synergies = {{109, spec.parameters[7]}};
        if (spec.skill == 102 || spec.skill == 114 || spec.skill == 118) {
            result.synergies = {{spec.skill == 102 ? 100 : spec.skill == 114 ? 105 : 110, spec.parameters[7]},
                {125, spec.parameters[6]}};
            ownerDamage(result);
        }
    }
    return result;
}
AuraDefinition evaluateAura(const AuraSkillSpec &spec, int rank,
    const std::map<int, int> &ranks, int fireMasteryPercent,
    int lightningMasteryPercent, int coldDamagePercent, int prayerRank) {
    auto result = evaluateBaseAura(spec, rank, ranks, prayerRank);
    const auto bonus = nativeAdd(100,nativeChecked(skillSynergyBonus(result.synergies,ranks)));
    const int mastery = result.element == 2 ? fireMasteryPercent :
        result.element == 3 ? lightningMasteryPercent : result.element == 4 ? coldDamagePercent : 0;
    result.minimumDamage=Fixed8::fromRaw(evaluateSkillMinimumDamageFixed(spec.minimumDamage,rank,spec.damageShift,bonus,mastery));
    result.maximumDamage=Fixed8::fromRaw(evaluateSkillDamageFixed(spec.maximumDamage,rank,spec.damageShift,bonus,mastery));
    ownerDamage(result);
    return result;
}
AuraDefinition evaluateNativeAura(const AuraSkillSpec &spec,int rank,const SkillFormulaCatalog &catalog,const SkillFormulaInput &input) {
    if(!input.actor || !input.stats || !input.statRules) throw std::invalid_argument("Missing native aura formula sources");
    if(spec.serverDoFunction!=65 && spec.serverDoFunction!=66 && spec.serverDoFunction!=81 && spec.serverDoFunction!=82)
        throw std::runtime_error("Native aura program requires its own attribute writer");
    const auto &record=catalog.skills.at(spec.skill);
    const auto &rules=*input.statRules;
    SkillFormulaValues values(catalog,input);
    const auto calc=[&](std::string_view field) {
        const auto value=values.calculation(FormulaDomain::Skill,spec.skill,rank,field);
        if(!value) throw std::runtime_error("Unavailable native aura calculation: "+std::string(field));
        return *value;
    };
    const auto parameter=[&](std::string_view token) {
        const auto value=values.parameter(FormulaDomain::Skill,spec.skill,rank,token);
        if(!value) throw std::runtime_error("Unavailable native aura parameter: "+std::string(token));
        return *value;
    };
    AuraDefinition result;
    result.skill=spec.skill;result.rank=rank;result.state=spec.state;result.ownerState=spec.ownerState;
    result.serverDoFunction=spec.serverDoFunction;
    result.filter=spec.filter;result.hitClass=spec.hitClass;result.resultFlags=spec.resultFlags;
    result.hostile=spec.skill==102 || spec.skill==114 || spec.skill==118 || spec.skill==119 || spec.skill==123;
    result.element=spec.element;result.elementalMultiplier=spec.parameters[4];
    result.radius=float(calc("aurarangecalc"));result.periodFrames=calc("perdelay");
    result.manaPerPulse=Fixed8::fromRaw(parameter("usmc"));
    const auto mana=values.stat("mana");
    if(!mana) throw std::runtime_error("Unavailable native aura mana source");
    const bool player=input.actor->ownerKind==StatOwnerKind::Player;
    const bool funded=*mana>=result.manaPerPulse.raw();
    const bool ownerFunded=!player || funded;
    std::vector<StatValue> target;
    if(spec.serverDoFunction==65 ? ownerFunded :
       (spec.serverDoFunction==66 || spec.serverDoFunction==81) && funded)
        target=values.auraStats(spec.skill,rank,6,true,spec.serverDoFunction==65);
    std::vector<StatValue> owner;
    std::vector<StatValue> extra;
    if(spec.serverDoFunction==65) {
        owner=target;
        // BasicAura writes the owner's extra passive columns after the aura
        // columns. SetStat overwrites a duplicate key (Fanaticism), not adds.
        if(*mana>result.manaPerPulse.raw() && record.passiveState<=0) {
            extra=values.passiveBuffStats(spec.skill,rank,true);
        }
    } else if(ownerFunded) {
        owner=values.passiveBuffStats(spec.skill,rank,true);
    }
    const auto validWrites=[&](std::vector<StatValue> writes) {
        std::vector<StatValue> attributes;
        for(const auto &write:writes) {
            if(!write.key.id) continue; // BasicAura's native valid-stat gate is strictly > 0.
            attributes.push_back(write);
        }
        return attributes;
    };
    result.targetWrites=validWrites(std::move(target));
    result.ownerWrites=validWrites(std::move(owner));
    result.ownerCallbackWrites=result.ownerWrites->size();
    // BasicAura's extra passives use direct list SetStat, independently of
    // the callback's direct-resource and duration procedure dispatch.
    for(const auto &write:extra) if(write.key.id) result.ownerWrites->push_back(write);
    const auto project=[&](const std::vector<StatValue> &writes,size_t callbackCount) {
        StatSnapshot snapshot;
        for(size_t index=0;index<writes.size();++index) {
            const auto &write=writes[index];
            const auto &rule=rules.definitions.at(write.key.id);
            if(index<callbackCount && (rule.direct || rule.name=="item_poisonlengthresist")) continue;
            writeNativeBaseStat(snapshot.values,rules,write.key,write.value);
            if(index<callbackCount && rule.name=="attackrate")
                writeNativeBaseStat(snapshot.values,rules,{rules.names.at("other_animrate"),0},write.value);
        }
        return nativeCharacterModifiers(snapshot,rules);
    };
    result.modifiers=project(*result.targetWrites,result.targetWrites->size());
    result.ownerModifiers=project(*result.ownerWrites,result.ownerCallbackWrites);
    if(result.element>=0) {
        result.minimumDamage=Fixed8::fromRaw(parameter("enms"));
        result.maximumDamage=Fixed8::fromRaw(parameter("exms"));
    }
    if(spec.serverDoFunction==82) {
        result.redemptionChance=calc("calc1");
        result.redemptionLife=Fixed8(calc("calc2"));result.redemptionMana=Fixed8(calc("calc3"));
    }
    return result;
}
} // namespace d2x
