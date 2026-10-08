#pragma once
#include "summon_spec.hpp"
#include "gameplay/effects/definition.hpp"
#include "gameplay/items/equipment_rules.hpp"
#include "world/identity.hpp"
#include "amazon_passive_spec.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/items/state.hpp"
#include <optional>

namespace d2x {
struct AmazonPetEquipment { int rank = 0; EquipmentSlot slot{}; std::string item; ItemQuality quality{}; };
struct AmazonSummonSpec {
    bool decoy = true, warp = false;
    std::array<int, 8> parameters{};
    CombatStateDefinition state;
    int gfxClass = -1, petType = 0;
    int appearOverlay = -1;
    float appearDuration = 0;
    int stateOverlay = -1, decoySkill = -1, penetrateSkill = -1;
    std::array<int, 3> maximumLife{}, attackChance{}, thinkFrames{};
    std::vector<AmazonPetEquipment> equipment;
    std::vector<std::pair<int, AmazonPassiveSpec>> inheritedPassives;
};
struct AmazonPetSpec {
    bool decoy = true, warp = false;
    int lifetimeFrames = 0;
    CombatStateDefinition state;
    int gfxClass = -1, petType = 0;
    int appearOverlay = -1;
    float appearDuration = 0;
    int stateOverlay = -1, lifeMinimum = 0, lifeMaximum = 0, lifePercent = 0;
    int itemLevel = 1, attackChance = 80, thinkFrames = 10;
    std::vector<AmazonPetEquipment> equipment;
};
SummonCastSpec resolveAmazonSummon(const SummonSkillSpec &, int rank, int ownerLevel,
    int difficulty, const CharacterAttributes &owner, const std::map<int, int> &hardRanks);
} // namespace d2x
