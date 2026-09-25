#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "content/monster_animation.hpp"
#include "gameplay/monsters/monster_spawn.hpp"
#include <array>
#include <map>
#include <optional>
#include <set>

namespace d2x {
struct MonsterRecord {
    std::string id, base, next, name, token, ai, spawn, sound, baseWeapon;
    std::string rightHandVariant, leftHandVariant;
    std::array<std::string, 8> specialVariants;
    size_t sourceRow = 0;
    int index = -1, rarity = 0, minGroup = 0, maxGroup = 0, partyMin = 0, partyMax = 0;
    int sparse = 0, alignment = 0, normalLevel = 0, transLevel = 0;
    int localBlood = 0, bleed = 0, overlayHeight = 0;
    std::optional<int> normalAttackRating;
    std::optional<int> normalAttackRating2;
    std::optional<int> normalDefense;
    std::optional<int> walkVelocity;
    std::optional<int> runVelocity;
    std::optional<MonsterNormalCombat> normalCombat;
    std::optional<MonsterProjectile> attack1Projectile;
    std::string attack1ProjectileArt;
    std::optional<MonsterProjectile> attack2Projectile;
    std::string attack2ProjectileArt;
    std::array<std::optional<MonsterSpell>, 4> spells;
    std::optional<MonsterResurrection> resurrection;
    std::optional<MonsterNest> nest;
    std::optional<MonsterWeb> web;
    std::array<std::optional<MonsterAiProfile>, 3> aiProfiles;
    bool enabled = false, randomSpawn = false, ranged = false, placeSpawn = false;
    bool killable = false, npc = false, critter = false, inert = false, boss = false;
    bool getHitMode = false, deadMode = false, skill2Mode = false, runMode = false;
    bool castMode = false, sequenceMode = false;
    bool castsShadow = false;
    std::array<std::string, 2> minions;
    bool hostile() const {
        return enabled && killable && !npc && !critter &&
               (!inert || ai == "FoulCrowNest") && alignment == 0;
    }
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
    std::map<MonsterKind, MonsterAttackTiming> attacks_;
    std::map<int, MonsterAttackTiming> hirelingAttacks_;
    std::map<MonsterKind, MonsterAttackTiming> attacks2_;
    std::map<MonsterKind, MonsterAttackTiming> casts_;
    std::map<MonsterKind, std::map<std::string, MonsterMotionTiming, std::less<>>> motions_;
    std::map<MonsterKind, std::map<std::string, std::string, std::less<>>> modeWeapons_;
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
    const MonsterAttackTiming *attackTiming(MonsterKind kind, int mode = 1) const {
        const auto &source = mode >= 3 ? casts_ : mode == 2 ? attacks2_ : attacks_;
        auto found = source.find(kind);
        return found == source.end() ? nullptr : &found->second;
    }
    const MonsterAttackTiming *hirelingAttackTiming(int classId) const {
        auto found = hirelingAttacks_.find(classId);
        return found == hirelingAttacks_.end() ? nullptr : &found->second;
    }
    const MonsterMotionTiming *motion(MonsterKind kind, std::string_view mode) const {
        auto kinds = motions_.find(kind);
        if (kinds == motions_.end()) return nullptr;
        auto found = kinds->second.find(mode);
        return found == kinds->second.end() ? nullptr : &found->second;
    }
    std::string_view modeWeapon(MonsterKind kind, std::string_view mode) const {
        auto kinds = modeWeapons_.find(kind);
        if (kinds == modeWeapons_.end()) return {};
        auto found = kinds->second.find(mode);
        return found == kinds->second.end() ? std::string_view{} : std::string_view(found->second);
    }
    const MonsterRecord *find(std::string_view id) const;
    const SuperUniqueRecord *superUnique(std::string_view id) const;
    MonsterPreset preset(int act, int index, int ds1Version) const;
};
} // namespace d2x
