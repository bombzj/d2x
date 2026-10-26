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
// Current 1019A3D0 constructs an ordered set, not a random engine. Its
// values are native one-based MagicPrefix identities; MPQ owns the rolls.
inline constexpr std::array<int,21> sceneCharmPrefixes{
    430,431,432,442,443,444,454,455,456,466,467,468,478,479,480,490,491,492,502,503,504};
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
