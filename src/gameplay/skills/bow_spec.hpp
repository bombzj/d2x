#pragma once
#include "gameplay/combat/damage_type.hpp"
#include "gameplay/skills/firewall_spec.hpp"
#include <array>

namespace d2x {
// Private arrow program. Importers own native records; gameplay receives values.
struct BowSkillSpec {
    int sourceDamage = 128;
    int conversionPercent = 0, conversionPerLevel = 0;
    DamageType element = DamageType::Magic;
    bool physicalSkillDamage = false;
    int freezePercent = 0;
    bool multiple = false;
    int activateFrames = 0;
    bool guided = false;
    bool strafe = false;
    int targetRadius = 0, minimumShots = 0;
    int retargetPeriod = 0, searchRadius = 0;
    int boltId = -1, centralArrows = 2;
    int freezingEjecta = -1;
    bool immolation = false;
    int fireRadius = 0, explosionRadius = 0;
    FirewallSpec fire;
    std::array<int, 5> fireMinimumPerLevel{}, fireMaximumPerLevel{};
    int fireSynergySkill = -1, fireSynergyPercent = 0;
    bool fireMastery = false;
};
} // namespace d2x
