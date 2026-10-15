#pragma once
#include "gameplay/monsters/mode_stats.hpp"
#include "gameplay/monsters/life_stats.hpp"
namespace d2x {
class DataTable;
std::shared_ptr<const MonsterModeLevelRatios> loadMonsterModeLevelRatios(const DataTable &);
std::shared_ptr<const MonsterModeTable> loadMonsterModeTable(const DataTable &,size_t row,int difficulty,std::shared_ptr<const MonsterModeLevelRatios>);
std::shared_ptr<const MonsterLifeLevelRatios> loadMonsterLifeLevelRatios(const DataTable &);
std::shared_ptr<const MonsterLifeTable> loadMonsterLifeTable(const DataTable &,size_t row,int difficulty,std::shared_ptr<const MonsterLifeLevelRatios>);
}
