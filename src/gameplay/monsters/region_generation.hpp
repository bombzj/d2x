#pragma once
#include "components.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace d2x {
struct MonsterRegionClass {
    std::string code;
    int nativeClass{},rarity{};
    bool spawn{},ranged{},hasComponents{};
    MonsterComponents components{};
};
struct MonsterRegionLevel {
    int count{};
    bool rangedFirst{};
    std::array<std::vector<int>,2> candidates;
};
struct MonsterRegionRules {
    std::map<int,MonsterRegionClass> classes;
    std::map<int,MonsterRegionLevel> levels;
};
struct MonsterRegionSelection {
    std::vector<int> classes;
    std::map<int,MonsterComponentPalette> components;
    uint64_t randomAfter{};
};
std::map<int,MonsterRegionSelection> initializeMonsterRegions(
    const MonsterRegionRules &,int difficulty,uint64_t &random);
struct MonsterPopulationChoice {int definition{},rarity{},replacement{-1};bool placeSpawn{};};
std::optional<int> choosePopulationMonster(std::span<const MonsterPopulationChoice>,
    std::span<const int> normalUniques,int difficulty,bool elite,uint64_t &roomRandom);
int choosePopulationGroup(int uniqueCount,int minimum,int maximum,int processedRooms,
    int populatedRooms,uint64_t &roomRandom);
}