#pragma once
#include "gameplay/monsters/kind.hpp"
#include "gameplay/combat/stats.hpp"
#include <array>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>
#include <utility>

namespace d2x {
struct NecroSummonSpec;
struct NecroPetSpec;
struct AmazonSummonSpec;
struct AmazonPetSpec;
struct AssassinPetDefinition;
struct AssassinPetSpec;
struct DruidPetDefinition;
struct DruidPetSpec;
struct NativeSummonStats {
    UnitCombatStats base;
    int skill{-1},rank{},lifePercent{};
    std::array<int,2> levelDefense{},levelRating{}; // ordinary / L MonLvl columns.
    std::optional<Fixed8> lifeOverride;
    Fixed8 initialLife;
    std::vector<StatValue> passiveAdds,auraWrites;
    std::vector<std::pair<int,int>> inheritedSkills;
    std::vector<std::pair<int,int>> prePassiveSkills;
    int resistSkill{-1},summonResistance{};
    bool materialBeforeStats{};
    bool setBaseStats{true}; // SkillNec::SetSummonBaseStats is a separate call.
    bool allowZeroLife{}; // Native untargetable units can have MAXHP == 0.
    bool assassinShadow{}; // Do49 evaluates its posted slots against the pet.
    std::optional<int> prePassiveLevel,finalLevel;
};
struct SummonSkillSpec {
    bool corpse = true, golem = false;
    std::shared_ptr<const NecroSummonSpec> necro;
    std::shared_ptr<const AmazonSummonSpec> amazon;
    std::shared_ptr<const AssassinPetDefinition> assassin;
    std::shared_ptr<const DruidPetDefinition> druid;
    std::string monster, iconArt;
    MonsterKind kind = MonsterKind::NecroSkeleton;
    int masterySkill = -1, resistSkill = -1, petType = 0;
    int masteryLife = 0, masteryDamage = 0;
    int lifePerRank = 0, damagePerRank = 0, attackPerRank = 0, defensePerRank = 0;
    int shieldChance = 0, shieldVariants = 0, resistMinimum = 0, resistMaximum = 0;
    std::array<int, 5> damageSteps{};
    std::array<UnitCombatStats, 3> base;
    std::vector<std::array<int, 3>> levelDefense, levelAttack;
    std::vector<std::array<int,3>> levelDefenseNonL,levelAttackNonL;
};
struct SummonCastSpec {
    bool corpse = true, golem = false;
    std::shared_ptr<const NecroPetSpec> necro;
    std::shared_ptr<const AmazonPetSpec> amazon;
    std::shared_ptr<const AssassinPetSpec> assassin;
    std::shared_ptr<const DruidPetSpec> druid;
    std::string monster;
    MonsterKind kind = MonsterKind::NecroSkeleton;
    UnitCombatStats stats;
    int limit = 0, shieldChance = 0, shieldVariants = 0, petType = 0;
    std::optional<NativeSummonStats> native;
};
} // namespace d2x
