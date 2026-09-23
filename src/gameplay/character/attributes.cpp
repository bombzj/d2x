#include "attributes.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
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
CharacterAttributes deriveCharacterAttributes(const CharacterDefinition &d, int level,
                                               const AttributeAllocation &a,
                                               const CharacterModifiers &m) {
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
    result.maxLife = bounded(int64_t(quarter(int64_t(d.lifeAdd) + d.vitality, d.lifePerLevel,
                                             int64_t(a.vitality) + m.vitality,
                                             d.lifePerVitality, level)) + m.maxLife, 1);
    result.maxMana = bounded(int64_t(quarter(d.energy, d.manaPerLevel,
                                             int64_t(a.energy) + m.energy,
                                             d.manaPerEnergy, level)) + m.maxMana, 1);
    result.maxStamina = bounded(int64_t(quarter(d.stamina, d.staminaPerLevel,
                                                int64_t(a.vitality) + m.vitality,
                                                d.staminaPerVitality, level)) + m.maxStamina, 1);
    result.attackRating = bounded(int64_t(result.dexterity) * 5 - 35 + d.toHitFactor + m.attackRating);
    result.defense = bounded(int64_t(result.dexterity) / 4 + m.defense);
    result.fireResist = m.fireResist;
    result.coldResist = m.coldResist;
    result.lightningResist = m.lightningResist;
    result.poisonResist = m.poisonResist;
    result.blockFactor = d.blockFactor;
    // CharStats.ManaRegen is an engine denominator, not mana per second.
    result.manaRegen = d.manaRegen > 0 ? float(result.maxMana) / d.manaRegen : 0;
    return result;
}
} // namespace d2x
