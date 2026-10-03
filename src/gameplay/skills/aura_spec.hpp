#pragma once
#include "gameplay/skills/aura.hpp"
#include "gameplay/skills/damage_curve.hpp"
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace d2x {
// Supported original formula vocabulary, prepared by the content adapter.
enum class AuraFormula {
    Linear12, Linear34, Linear56, Parameter5, One, HalfLinear56,
    Diminishing34, RemainingDiminishing34, Diminishing56, ElementalMinimum,
    PrayerMinimum, NegativeDiminishing34, NegativeDiminishing56,
    NegativeParameter5, NegativeCappedLinear34, AttackRating, BaseRank
};
enum class AuraStat {
    PoisonLength, Life, StaminaRecovery, StaminaPercent, ManaRecovery,
    DamagePercent, AttackRatingPercent, AttackRate, AnimationRate, Velocity,
    DefensePercent, FireResist, ColdResist, LightningResist, PhysicalResist,
    FireMaximum, ColdMaximum, LightningMaximum, Thorns, Concentration
};
struct AuraStatFormula {
    AuraFormula formula;
    // A missing stat preserves the legacy stateless-Thorns ignored field.
    std::optional<AuraStat> stat;
};
struct AuraSkillSpec {
    int skill = -1;
    std::array<int, 8> parameters{};
    int mana = 0, manaPerLevel = 0, manaShift = 0;
    uint32_t filter = 0, resultFlags = 0;
    int hitClass = 13, periodFrames = 0;
    AuraFormula radius = AuraFormula::One;
    std::optional<AuraFormula> period;
    std::array<AuraFormula, 3> redemption{};
    CombatStateDefinition ownerState, state;
    std::vector<AuraStatFormula> stats;
    int toHit = 0, toHitPerLevel = 0;
    SkillDamageCurve minimumDamage, maximumDamage;
    int damageShift = 0, element = -1;
    std::optional<SkillDamageCurve> prayerMinimum;
    int prayerShift = 0;
};
} // namespace d2x
