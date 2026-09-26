#include "attributes.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
float manaRecoveryRate(int maximumMana, int denominator, int recoveryBonus) {
    const int64_t frames = denominator > 0 ? int64_t(25) * denominator : 7500;
    const int64_t base = std::max<int64_t>(1, int64_t(maximumMana) * 256 / frames);
    return float(base * std::max(0, 100 + recoveryBonus) / 100) * 25.f / 256.f;
}
int64_t allocatedPoints(const AttributeAllocation &a) {
    return int64_t(a.strength) + a.dexterity + a.vitality + a.energy;
}
bool allocateAttribute(AttributeAllocation &a, int &unspent, Attribute attribute) {
    if (unspent <= 0) return false;
    int *target = nullptr;
    switch (attribute) {
    case Attribute::Strength: target = &a.strength; break;
    case Attribute::Dexterity: target = &a.dexterity; break;
    case Attribute::Vitality: target = &a.vitality; break;
    case Attribute::Energy: target = &a.energy; break;
    }
    if (!target || *target == std::numeric_limits<int>::max()) return false;
    ++*target;
    --unspent;
    return true;
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
    add(a.attackRating, b.attackRating); add(a.defense, b.defense);
    add(a.fireResist, b.fireResist); add(a.coldResist, b.coldResist);
    add(a.lightningResist, b.lightningResist); add(a.poisonResist, b.poisonResist);
    add(a.lightRadius, b.lightRadius);
    add(a.fasterMoveVelocity, b.fasterMoveVelocity);
    add(a.velocityPercent, b.velocityPercent);
    add(a.staminaDrainPercent, b.staminaDrainPercent);
    add(a.staminaRecoveryBonus, b.staminaRecoveryBonus);
    a.torsoSpeed = std::max(a.torsoSpeed, b.torsoSpeed);
    mergeCombatModifiers(a.combat, b.combat);
}
CharacterAttributes deriveCharacterAttributes(const CharacterDefinition &d, int level,
                                               const AttributeAllocation &a,
                                               const CharacterModifiers &m,
                                               int resistancePenalty) {
    if (level < 1 || a.strength < 0 || a.dexterity < 0 || a.vitality < 0 || a.energy < 0)
        throw std::runtime_error("Invalid character progression");
    CharacterAttributes result;
    auto bounded = [](int64_t value, int minimum = 0) {
        if (value < minimum || value > std::numeric_limits<int>::max())
            throw std::runtime_error("Character attribute exceeds supported range");
        return int(value);
    };
    result.strength = bounded(int64_t(d.strength) + a.strength + m.strength);
    result.dexterity = bounded(int64_t(d.dexterity) + a.dexterity + m.dexterity);
    result.vitality = bounded(int64_t(d.vitality) + a.vitality + m.vitality);
    result.energy = bounded(int64_t(d.energy) + a.energy + m.energy);
    auto quarter = [](int64_t base, int perLevel, int64_t points, int perPoint, int level) {
        int64_t value = base * 4 + int64_t(level - 1) * perLevel + points * perPoint;
        if (value <= 0 || value / 4 > std::numeric_limits<int>::max())
            throw std::runtime_error("Character resource exceeds supported range");
        return int(value / 4);
    };
    const int naturalLife = quarter(int64_t(d.lifeAdd) + d.vitality, d.lifePerLevel,
                                    int64_t(a.vitality) + m.vitality, d.lifePerVitality, level);
    const int naturalMana = quarter(d.energy, d.manaPerLevel,
                                    int64_t(a.energy) + m.energy, d.manaPerEnergy, level);
    result.maxLife = bounded(int64_t(naturalLife) * std::max<int64_t>(0, 100LL + m.combat.lifePercent) / 100 +
                             m.maxLife, 1);
    result.maxMana = bounded(int64_t(naturalMana) * std::max<int64_t>(0, 100LL + m.combat.manaPercent) / 100 +
                             m.maxMana, 1);
    result.maxStamina = bounded(int64_t(quarter(d.stamina, d.staminaPerLevel,
                                                int64_t(a.vitality) + m.vitality,
                                                d.staminaPerVitality, level)) + m.maxStamina, 1);
    result.attackRating = bounded((int64_t(result.dexterity) * 5 - 35 + d.toHitFactor + m.attackRating) *
                                  std::max<int64_t>(0, 100LL + m.combat.attackRatingPercent) / 100);
    result.defense = bounded(int64_t(result.dexterity) / 4 + m.defense);
    auto resistance = [resistancePenalty](int value, int maximumBonus) {
        const int maximum = int(std::clamp(int64_t(75) + maximumBonus, int64_t(-100), int64_t(95)));
        return int(std::clamp(int64_t(value) + resistancePenalty, int64_t(-100), int64_t(maximum)));
    };
    result.fireResist = resistance(m.fireResist, m.combat.fireMaxResist);
    result.coldResist = resistance(m.coldResist, m.combat.coldMaxResist);
    result.lightningResist = resistance(m.lightningResist, m.combat.lightningMaxResist);
    result.poisonResist = resistance(m.poisonResist, m.combat.poisonMaxResist);
    result.lightRadius = int(std::clamp(int64_t(13) + m.lightRadius,
                                        int64_t(1), int64_t(18)));
    // D2Common applies diminishing returns to item FRW, then adds the ordinary
    // velocity stat (including armor penalties and the run mode bonus).
    if (m.fasterMoveVelocity < 0)
        throw std::runtime_error("Negative item faster movement is unsupported");
    const int64_t fasterMove = m.fasterMoveVelocity
        ? int64_t(m.fasterMoveVelocity) * 150 / (int64_t(m.fasterMoveVelocity) + 150) : 0;
    const int64_t movementPercent = 100 + fasterMove + m.velocityPercent;
    const int64_t walkPercent = std::max<int64_t>(25, movementPercent);
    const int64_t runBonus = int64_t(100) * d.runVelocity / d.walkVelocity - 100;
    const int64_t runPercent = std::max<int64_t>(25, movementPercent + runBonus);
    constexpr float velocityScale = 25.f / 16.f; // 8.8 path velocity, 25 game frames per second.
    result.walkSpeed = float(d.walkVelocity) * velocityScale * float(walkPercent) / 100.f;
    result.runSpeed = float(d.walkVelocity) * velocityScale * float(runPercent) / 100.f;
    result.walkAnimationRate = 25.f * 213.f / 256.f * float(walkPercent) / 100.f;
    result.runAnimationRate = 25.f * 101.f / 256.f * float(runPercent) / 100.f;
    const int64_t torsoMultiplier = int64_t(m.torsoSpeed) / 10 + 1;
    int64_t drain = int64_t(2) * d.runDrain * torsoMultiplier;
    drain += drain * m.staminaDrainPercent / -100;
    drain = std::max<int64_t>(1, drain);
    result.staminaDrain = float(drain) * 25.f / 256.f;
    result.staminaRecoveryBonus = m.staminaRecoveryBonus;
    result.combat = m.combat;
    result.blockFactor = d.blockFactor;
    // CharStats.ManaRegen is an engine denominator, not mana per second.
    result.manaRegen = manaRecoveryRate(result.maxMana, d.manaRegen, m.combat.manaRecovery);
    return result;
}
} // namespace d2x
