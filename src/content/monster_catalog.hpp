#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include <array>
#include <map>
#include <optional>
#include <set>

namespace d2x {
struct MonsterRecord {
    std::string id, base, next, name, token, ai, spawn;
    int index = -1, rarity = 0, minGroup = 0, maxGroup = 0, partyMin = 0, partyMax = 0;
    int sparse = 0, alignment = 0, normalLevel = 0;
    std::optional<int> normalAttackRating;
    bool enabled = false, randomSpawn = false, ranged = false, placeSpawn = false;
    bool killable = false, npc = false, critter = false, inert = false, boss = false;
    std::array<std::string, 2> minions;
    bool hostile() const { return enabled && killable && !npc && !critter && !inert && alignment == 0; }
};
struct SuperUniqueRecord {
    std::string id, monster, name;
    int index = -1, minGroup = 0, maxGroup = 0;
    std::array<int, 3> modifiers{};
    std::array<std::string, 3> treasureClasses;
};
enum class MonsterPresetKind { Unknown, Monster, SuperUnique, Place };
struct MonsterPreset {
    MonsterPresetKind kind = MonsterPresetKind::Unknown;
    std::string id;
};
class MonsterCatalog {
    bool supported_ = false;
    std::map<std::string, MonsterRecord, std::less<>> monsters_;
    std::map<int, std::string> indices_;
    std::set<std::string, std::less<>> ambiguous_;
    std::map<std::string, SuperUniqueRecord, std::less<>> uniques_;
    std::array<std::vector<std::string>, 5> presets_;
    std::set<std::string, std::less<>> places_;
    std::vector<std::string> diagnostics_;
    int championChance_ = 0;

  public:
    MonsterCatalog(Archives &archives, const DataTable &monstats);
    bool supported() const { return supported_; }
    int championChance() const { return championChance_; }
    const auto &diagnostics() const { return diagnostics_; }
    const auto &monsters() const { return monsters_; }
    const MonsterRecord *find(std::string_view id) const;
    const SuperUniqueRecord *superUnique(std::string_view id) const;
    MonsterPreset preset(int act, int index, int ds1Version) const;
};
} // namespace d2x
