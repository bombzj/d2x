#pragma once
#include "gameplay/model/definitions.hpp"
#include "gameplay/monsters/unique_modifiers.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace d2x {
enum class MonsterRank { Normal, Minion, Champion, Unique, SuperUnique, Boss };
enum class SpawnOrigin { Density, Preset, Debug, Summoned };
struct PopulationSettings {
    uint32_t seed = 0; // Supplied by the application/session; zero is a valid explicit seed.
    int difficulty = 0; // Normal, Nightmare, Hell. Independent of loot randomness.
};
// Content identity survives implementation substitution, travel, death and saving.
struct MonsterIdentity {
    std::string monster, superUnique, spawnKey;
    MonsterRank rank = MonsterRank::Normal;
    SpawnOrigin origin = SpawnOrigin::Density;
    uint32_t group = 0;
    std::optional<MonsterEnchantment> enchantment = std::nullopt;
    std::string ownerSpawnKey = {}; // Original setboss party ownership within this region.
    bool championVariantAllowed = true;
};
struct MonsterSpawn {
    MonsterIdentity identity;
    MonsterKind kind = MonsterKind::Fallen;
    Vec position;
    std::vector<Vec> skillPositions = {};
};
struct MonsterImplementation {
    MonsterKind kind;
    bool substitute;
};
struct MonsterAccuracy {
    int level = 1;
    int attackRating = 0;
};
struct MonsterDefense {
    int level = 1;
    int defense = 0;
    bool demon = false, undead = false, boss = false;
};
enum class MonsterDamageType { Physical, Magic, Fire, Lightning, Cold, Poison };
struct MonsterElementAttack {
    std::string mode, type;
    int chance = 0, minimum = 0, maximum = 0, durationFrames = 0;
};
// Resolved from the original MonStats and MonLvl tables. Some ordinary monsters
// have life but no A1 melee columns, so the attacks are independent.
struct MonsterNormalCombat {
    int minLife = 0, maxLife = 0;
    std::optional<std::pair<int, int>> attack1Damage;
    std::optional<std::pair<int, int>> attack2Damage;
    std::array<std::optional<MonsterElementAttack>, 3> elements;
};
enum class MonsterAiKind { Skeleton, Brute, Zombie, Fallen, CorruptRogue, Goatman, QuillRat,
                           Wraith, CorruptLancer, CorruptArcher, SkeletonBow, Bighead,
                           SkeletonMage, Fetish, Vampire, FallenShaman, FoulCrowNest, BloodHawk,
                           Arach, Smith, Griswold, BloodRaven, Countess, Andariel };
struct MonsterAiProfile {
    MonsterAiKind kind;
    std::array<int, 8> params{};
    int meleeRange = 0; // Resolved MonStats2.MeleeRng, including the 255 weapon-class sentinel.
    int retreatVelocityBonus = 0;
};
struct MonsterAttackTiming {
    float duration = 0;
    float impact = 0;
    int frames = 0;
    int sequenceFrames = 0;
    std::vector<float> eventTimes = {};
};
struct MonsterProjectile {
    int id = -1;
    float velocity = 0, lifetime = 0;
    int minimumDamage = 0, maximumDamage = 0, sourceDamage = 0;
};
struct MonsterFirewall {
    int makerId = -1, fireId = -1, makerFrames = 0, fireFrames = 0;
    float velocity = 0;
    int minimumDamage = 0, maximumDamage = 0, hitShift = 0, size = 1;
};
struct TowerReward {
    int lifetimeFrames = 0, openingFrame = 0, goldInterval = 0, radius = 0;
};
struct MonsterSpell {
    std::string sourceSkill, mode, art, element;
    MonsterProjectile projectile;
    int minimumDamage = 0, maximumDamage = 0;
    int poisonFrames = 0, hitShift = 8;
    bool killOnHit = true;
};
struct MonsterResurrection {
    std::string sourceSkill, mode, minion;
};
struct MonsterNest {
    std::string sourceSkill, mode, child, sequence;
    int spawnX = 0, spawnY = 0;
};
struct MonsterWeb {
    std::string sourceSkill, mode, art;
    int missileId = -1;
    float lifetime = 0, radius = 0, auraDuration = 0, slowDuration = 0;
    int slowPercent = 0;
};
// Explicit implementation registry: add real actors here as their behaviour/assets land.
MonsterImplementation monsterImplementation(const std::string &code);
const char *monsterRankName(MonsterRank rank);
} // namespace d2x
