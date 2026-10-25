#pragma once
#include "gameplay/stats/stat_list.hpp"
#include <cstdint>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace d2x::pd2 {
struct MapLootRules {
    struct Chance {uint16_t denominator{},factor{};};
    struct UniqueMap {std::string code;int uniqueRow{-1};};
    std::map<uint16_t,std::string> treasures;
    std::map<int,Chance> specialChances;
    std::array<std::set<int>,3> tiers;
    std::array<UniqueMap,8> uniqueMaps;
    std::set<int> bosses;
    std::set<int> uniqueMapBosses;
};
struct MapMonsterLoot {
    StatSnapshot stats;
    bool boss{},minion{};
    int area{};
    int difficulty{},players{1},nativeClass{-1};
    bool undead{};
};
// Empty treasure means rerun this monster's original TC. The visitor advances
// the same unit stream, so successive bonuses observe all intervening TC draws.
using MapTreasureVisitor=std::function<bool(std::string_view,uint64_t &)>;
bool visitMapBonusTreasure(const MapLootRules &,const MapMonsterLoot &,uint64_t &,const MapTreasureVisitor &);
using SpecialDropVisitor=std::function<bool(std::string_view,int uniqueRow,uint64_t &)>;
bool visitSpecialMonsterDrops(const MapLootRules &,const MapMonsterLoot &,uint64_t &,const SpecialDropVisitor &);
}
