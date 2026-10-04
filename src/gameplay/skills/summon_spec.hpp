#pragma once
#include "gameplay/monsters/kind.hpp"
#include "gameplay/combat/stats.hpp"
#include <array>
#include <string>
#include <vector>
#include <memory>
#include <map>

namespace d2x {
struct NecroSummonSpec;
struct NecroPetSpec;
struct AmazonSummonSpec;
struct AmazonPetSpec;
struct SummonSkillSpec {
    bool corpse = true, golem = false;
    std::shared_ptr<const NecroSummonSpec> necro;
    std::shared_ptr<const AmazonSummonSpec> amazon;
    std::string monster, iconArt;
    MonsterKind kind = MonsterKind::NecroSkeleton;
    int masterySkill = -1, resistSkill = -1;
    int masteryLife = 0, masteryDamage = 0;
    int lifePerRank = 0, damagePerRank = 0, attackPerRank = 0, defensePerRank = 0;
    int shieldChance = 0, shieldVariants = 0, resistMinimum = 0, resistMaximum = 0;
    std::array<int, 5> damageSteps{};
    std::array<UnitCombatStats, 3> base;
    std::vector<std::array<int, 3>> levelDefense, levelAttack;
};
struct SummonCastSpec {
    bool corpse = true, golem = false;
    std::shared_ptr<const NecroPetSpec> necro;
    std::shared_ptr<const AmazonPetSpec> amazon;
    std::string monster;
    MonsterKind kind = MonsterKind::NecroSkeleton;
    UnitCombatStats stats;
    int limit = 0, shieldChance = 0, shieldVariants = 0;
};
} // namespace d2x
