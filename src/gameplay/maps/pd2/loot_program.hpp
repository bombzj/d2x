#pragma once
#include "gameplay/stats/stat_list.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace d2x::pd2 {
struct MapLootRules {
    std::map<uint16_t,std::string> treasures;
    std::set<int> bosses;
};
struct MapMonsterLoot {
    StatSnapshot stats;
    bool boss{},minion{};
    int area{};
};
// Empty treasure means rerun this monster's original TC. The visitor advances
// the same unit stream, so successive bonuses observe all intervening TC draws.
using MapTreasureVisitor=std::function<bool(std::string_view,uint64_t &)>;
bool visitMapBonusTreasure(const MapLootRules &,const MapMonsterLoot &,uint64_t &,const MapTreasureVisitor &);
}
