#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "content/monsters/monster_animation.hpp"
#include "gameplay/monsters/kind.hpp"
#include "gameplay/monsters/combat_values.hpp"
#include "gameplay/monsters/ai_spec.hpp"
#include "gameplay/monsters/ability_spec.hpp"
#include "gameplay/skills/firewall_spec.hpp"
#include "gameplay/loot/tower_reward.hpp"
#include "world/navigation.hpp"
#include "gameplay/monsters/collision_spec.hpp"
#include <array>
#include <map>
#include <optional>
#include <set>

namespace d2x {
struct SpearSequence;
struct MonsterRecord {
    std::string id, base, next, name, token, ai, spawn, sound, baseWeapon, resurrectionMode;
    std::string rightHandVariant, leftHandVariant;
    std::vector<std::string> shieldVariants;
    std::array<std::string, 8> specialVariants;
    std::array<std::string, 16> components;
    size_t sourceRow = 0;
    int index = -1, rarity = 0, minGroup = 0, maxGroup = 0, partyMin = 0, partyMax = 0;
    int sparse = 0, alignment = 0, normalLevel = 0, transLevel = 0;
    std::array<int, 3> coldEffect{};
    std::array<int, 3> uniqueTrans{-1, -1, -1};
    int localBlood = 0, bleed = 0, overlayHeight = 0;
    int lightRadius = 0;
    std::array<int, 3> lightColor{}; // MonStats2.Light and light-r/g/b, real identity.
    int collisionSize = 0, spawnCollision = 0, hitClass = 0, meleeRange = 0;
    std::optional<int> normalAttackRating;
    std::optional<int> normalAttackRating2;
    std::optional<int> normalDefense;
    std::optional<int> walkVelocity;
    std::optional<int> runVelocity;
    std::optional<int> walkAnimationRate, runAnimationRate;
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
    bool killable = false, npc = false, interact = false, critter = false, inert = false, boss = false;
    bool getHitMode = false, deadMode = false, skill2Mode = false, runMode = false;
    bool castMode = false, sequenceMode = false;
    std::shared_ptr<const SpearSequence> hirelingSequence;
    std::optional<MonsterAttackTiming> hirelingCastTiming;
    int infernoLength = 0, infernoAnimation = 0;
    bool curseable = false;
    bool switchAi = false;
    bool castsShadow = false, corpseSelectable = false;
    bool demon = false, undead = false, ownsParty = false, primeEvil = false;
    bool flying = false;
    std::array<std::string, 2> minions;
    MovementCollisionRule movementRule() const {
        // PATH_AllocDynamicPath: Wraith's small pattern ignores walls, flying
        // uses barriers. Door opening still requires a separate object action.
        return monsterMovementCollision(base == "wraith1", flying, false, collisionSize);
    }
    MovementCollisionRule spawnRule() const {
        // MonsterSpawn uses MonStats2.spawnCol and CheckMaskWithSize, not
        // PATH's flying/Wraith masks or small-unit path pattern.
        return monsterSpawnCollision(spawnCollision, collisionSize);
    }
    bool hostile() const {
        return enabled && killable && !npc && !critter &&
               (!inert || ai == "FoulCrowNest" || id == "prisondoor") && alignment == 0;
    }
};
int monsterMovementPercent(const MonsterRecord &record, int difficulty, int percentage, bool chilled);
struct SuperUniqueRecord {
    std::string id, monster, name;
    int index = -1, minGroup = 0, maxGroup = 0;
    std::array<int, 3> modifiers{};
    std::array<int, 3> uniqueTrans{};
    bool autoPosition = false, stacks = false;
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
    std::map<int, std::map<std::string, MonsterMotionTiming, std::less<>>> hirelingMotions_;
    std::map<MonsterKind, MonsterAttackTiming> attacks2_;
    std::map<MonsterKind, MonsterAttackTiming> casts_;
    std::map<MonsterKind, MonsterAttackTiming> quickAttacks_;
    std::map<MonsterKind, std::map<std::string, MonsterMotionTiming, std::less<>>> motions_;
    std::map<MonsterKind, std::map<std::string, std::string, std::less<>>> modeWeapons_;
    std::map<int, std::string> indices_;
    std::map<int, MonsterPreset> nativePresets_;
    std::set<std::string, std::less<>> ambiguous_;
    std::map<std::string, SuperUniqueRecord, std::less<>> uniques_;
    std::array<std::vector<std::string>, 5> presets_;
    std::set<std::string, std::less<>> places_;
    std::vector<std::string> diagnostics_;
    int championChance_ = 0;
    std::optional<MonsterFirewall> countessFirewall_;
    std::optional<TowerReward> towerReward_;

  public:
    MonsterCatalog(Archives &archives, const DataTable &monstats);
    bool supported() const { return supported_; }
    int championChance() const { return championChance_; }
    const auto &countessFirewall() const { return countessFirewall_; }
    const auto &towerReward() const { return towerReward_; }
    const auto &diagnostics() const { return diagnostics_; }
    const auto &monsters() const { return monsters_; }
    const MonsterAttackTiming *attackTiming(MonsterKind kind, int mode = 1) const {
        if (mode == 4 && kind == MonsterKind::Andariel) mode = 1;
        if (mode == 4 && kind == MonsterKind::BloodRaven) {
            auto found = quickAttacks_.find(kind);
            return found == quickAttacks_.end() ? nullptr : &found->second;
        }
        const auto &source = mode >= 3 ? casts_ : mode == 2 ? attacks2_ : attacks_;
        auto found = source.find(kind);
        return found == source.end() ? nullptr : &found->second;
    }
    const MonsterAttackTiming *hirelingAttackTiming(int classId) const {
        auto found = hirelingAttacks_.find(classId);
        return found == hirelingAttacks_.end() ? nullptr : &found->second;
    }
    const MonsterMotionTiming *hirelingMotion(int classId, std::string_view mode) const {
        auto actor = hirelingMotions_.find(classId);
        if (actor == hirelingMotions_.end()) return nullptr;
        auto found = actor->second.find(mode);
        return found == actor->second.end() ? nullptr : &found->second;
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
    MonsterPreset preset(int act, int index, int ds1Version, bool nativeIdentity = false) const;
};
} // namespace d2x
