#pragma once
#include "gameplay/character/allocation.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include "gameplay/combat/stat_modifiers.hpp"

namespace d2x {
struct CharacterDefinition {
    std::string name, code, appearance;
    size_t sourceRow = 0;
    int strength = 0, dexterity = 0, vitality = 0, energy = 0;
    int stamina = 0, lifeAdd = 0;
    int lifePerLevel = 0, manaPerLevel = 0, staminaPerLevel = 0;
    int lifePerVitality = 0, manaPerEnergy = 0, staminaPerVitality = 0;
    int statPerLevel = 0, manaRegen = 0, toHitFactor = 0, blockFactor = 0;
    int walkVelocity = 0, runVelocity = 0, runDrain = 0;
};
struct CharacterModifiers {
    int strength = 0, dexterity = 0, vitality = 0, energy = 0;
    int maxLife = 0, maxMana = 0, maxStamina = 0;
    int attackRating = 0, defense = 0;
    int fireResist = 0, coldResist = 0, lightningResist = 0, poisonResist = 0;
    int lightRadius = 0;
    int fasterMoveVelocity = 0, velocityPercent = 0, otherAnimationRate = 0;
    int staminaPercent = 0;
    int torsoSpeed = 0, staminaDrainPercent = 0, staminaRecoveryBonus = 0;
    CombatModifiers combat;
};
struct CharacterAttributes {
    int strength = 0, dexterity = 0, vitality = 0, energy = 0;
    int maxLife = 1, maxMana = 1, maxStamina = 1;
    int attackRating = 0, defense = 0;
    int baseAttackRating = 0; // Before percentage and selected-weapon contributions.
    int fireResist = 0, coldResist = 0, lightningResist = 0, poisonResist = 0;
    int lightRadius = 13;
    float walkSpeed = 0, runSpeed = 0, staminaDrain = 0;
    float walkAnimationRate = 0, runAnimationRate = 0;
    int staminaRecoveryBonus = 0, staminaPercent = 0, otherAnimationRate = 0;
    CombatModifiers combat;
    int blockFactor = 0;
    float manaRegen = 0;
};
float manaRecoveryRate(int maximumMana, int denominator, int recoveryBonus);
void mergeCharacterModifiers(CharacterModifiers &target, const CharacterModifiers &source);
CharacterAttributes deriveCharacterAttributes(const CharacterDefinition &definition, int level,
                                               const AttributeAllocation &allocation,
                                               const CharacterModifiers &modifiers = {},
                                               int resistancePenalty = 0);
} // namespace d2x
