#pragma once
#include <array>
#include <map>
#include <memory>
namespace d2x {
using MonsterLifeLevelRatios=std::map<int,std::array<int,6>>;
struct MonsterLifeTable {
    bool noRatio{};
    int minimum{},maximum{};
    std::shared_ptr<const MonsterLifeLevelRatios> levels;
};
struct MonsterLifeBounds {int minimum{},maximum{};};
// DATATBLS_CalculateMonsterStatsByLevel, HP mask only; values are whole HP.
MonsterLifeBounds monsterLifeBounds(const MonsterLifeTable &,int level,int difficulty,int gameType);
}
