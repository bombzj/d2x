#pragma once
#include "core/math.hpp"
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace d2x {
enum class Skill { Fireball, FrostNova, Whirlwind, Teleport, Leap, WarCry, FireBolt, StaticField, Count };
constexpr size_t skillCount = size_t(Skill::Count);
enum class MonsterKind { Fallen, Zombie, Skeleton, CorruptRogue, Brute, Goatman, QuillRat, Count };
// Native Levels.txt IDs. Template previews occupy a separate range (10000 + Def).
enum class RegionId { Encampment = 1 };
enum class Interaction { None, Talk, Heal, Travel, Stash };

struct SkillDefinition {
    Skill id;
    const char *name, *shortName, *description;
    float manaCost, cooldown, castDuration, damage, radius, range, duration, chill, stun;
    float projectileSpeed = 0;
};
struct MonsterDefinition {
    MonsterKind id;
    const char *token;
    float maxLife, speed, damage, attackInterval, sightRange, attackRange;
};
struct PlayerRules {
    float walkSpeed = 5, runSpeed = 9, spinSpeed = 8;
    float staminaDrain = 7, staminaRegen = 14;
    float meleeRange = 2, meleeRadius = 1.2f, meleeDuration = .48f;
};
struct RegionDefinition {
    RegionId id;
    std::string name, mapPath;
    bool safe = false;
};
const SkillDefinition &skillDefinition(Skill id);
const MonsterDefinition &monsterDefinition(MonsterKind id);
const PlayerRules &playerRules();
} // namespace d2x
