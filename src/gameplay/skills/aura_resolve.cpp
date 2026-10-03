#include "aura_resolve.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
int evaluate(AuraFormula formula, const AuraSkillSpec &spec, int rank,
             const std::map<int, int> &ranks, int prayerRank) {
    auto parameter = [&](int index) { return spec.parameters[size_t(index - 1)]; };
    auto linear = [&](int first) { return parameter(first) + (rank - 1) * parameter(first + 1); };
    auto diminishing = [&](int first) {
        const int low = parameter(first), high = parameter(first + 1);
        return std::min(high, low + (high - low) * 110 * rank / (rank + 6) / 100);
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
        // Retain the original float -> fixed-point conversion boundary.
        return int(evaluateSkillDamage(spec.minimumDamage, rank, spec.damageShift) * 256.f);
    case AuraFormula::PrayerMinimum:
        if (prayerRank <= 0) return 0;
        if (!spec.prayerMinimum) throw std::runtime_error("Missing prepared Prayer damage curve");
        return int(evaluateSkillDamage(*spec.prayerMinimum, prayerRank, spec.prayerShift) * 256.f);
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
    case AuraStat::Life: result.lifePerPulse = float(amount) / 256.f; break;
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
    const int minimum = result.skill == 118 ? 1 : int(result.minimumDamage * result.elementalMultiplier);
    const int maximum = int(result.maximumDamage * result.elementalMultiplier);
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
    result.rank = rank;
    result.manaPerPulse = float((int64_t(spec.mana) + int64_t(rank - 1) * spec.manaPerLevel) << spec.manaShift) / 256.f;
    result.filter = spec.filter;
    result.hitClass = spec.hitClass;
    result.resultFlags = spec.resultFlags;
    result.radius = float(value(spec.radius));
    result.periodFrames = spec.period ? value(*spec.period) : spec.periodFrames;
    result.hostile = spec.skill == 66 || spec.skill == 102 || spec.skill == 114 ||
        spec.skill == 118 || spec.skill == 119 || spec.skill == 123;
    if (spec.skill == 124) {
        result.redemptionChance = value(spec.redemption[0]);
        result.redemptionLife = float(value(spec.redemption[1]));
        result.redemptionMana = float(value(spec.redemption[2]));
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
        result.minimumDamage = evaluateSkillDamage(spec.minimumDamage, rank, spec.damageShift);
        result.maximumDamage = evaluateSkillDamage(spec.maximumDamage, rank, spec.damageShift);
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
    int bonus = 100;
    for (const auto &[synergy, percent] : result.synergies)
        if (const auto found = ranks.find(synergy); found != ranks.end()) bonus += found->second * percent;
    if (spec.skill != 118) result.minimumDamage = float(int64_t(result.minimumDamage * 256.f) * bonus / 100) / 256.f;
    result.maximumDamage = float(int64_t(result.maximumDamage * 256.f) * bonus / 100) / 256.f;
    const int mastery = result.element == 2 ? fireMasteryPercent :
        result.element == 3 ? lightningMasteryPercent : result.element == 4 ? coldDamagePercent : 0;
    auto mastered = [&](float damage) {
        const int64_t fixed = int64_t(damage * 256.f);
        return float(fixed + fixed * mastery / 100) / 256.f;
    };
    result.minimumDamage = mastered(result.minimumDamage);
    result.maximumDamage = mastered(result.maximumDamage);
    ownerDamage(result);
    return result;
}
} // namespace d2x
