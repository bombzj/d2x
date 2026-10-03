#pragma once
#include "gameplay/effects/definition.hpp"
#include <array>
#include <map>
#include <string>

namespace d2x {
// Weapon skills use the ordinary attack animation, equipment and ammunition pipeline.
struct WeaponSkillSpec {
    std::string requiredType;
    bool thrown = false, manaOnRelease = false;
    int attackRating = 0, attackRatingPerLevel = 0, delayFrames = 0;
    int damagePercent = 0, damagePerLevel = 0, selfDamagePercent = 0;
    std::map<int, int> damageSynergies;
    int damageStartLevel = 1, attacks = 1, attackLimit = 1, rollbackPercent = 0;
    bool interruptible = true;
    std::array<int, 3> elementPercent{};
    int elementPerLevel = 0;
    std::array<std::map<int, int>, 3> elementSynergies;
    bool smite = false;
    std::string mode;
    int stunFrames = 0, stunPerLevel = 0;
    int conversionMinimum = 0, conversionMaximum = 0, conversionChance = 0, conversionFrames = 0;
    CombatStateDefinition conversionState;
    int chargeVelocity = 0;
};
} // namespace d2x
