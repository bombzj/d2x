#pragma once
#include "core/math.hpp"
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace d2x {
// Execution behavior is not a Skills.txt identity or a persistent ID.
enum class SkillBehavior { None, Fireball, FrostNova, Teleport, FireBolt, StaticField, IceBolt, Nova, IceBlast, ChargedBolt, FrozenArmor, Inferno, WeaponProjectile, RaiseSkeleton, FrozenOrb, Blizzard, GlacialSpike, ShiverArmor };
enum class MonsterKind { Fallen, Zombie, Skeleton, CorruptRogue, Brute, Goatman, QuillRat,
                         Wraith, CorruptLancer, CorruptArcher, SkeletonBow, Bighead,
                         HellBovine, SkeletonMage, Fetish, Vampire, FallenShaman,
                         FoulCrowNest, BloodHawk, Arach, NecroSkeleton, Count };
// Native Levels.txt IDs. Template previews occupy a separate range (10000 + Def).
enum class RegionId { Encampment = 1 };
enum class Interaction { None, Talk, Heal, Travel, Stash, Loot, Shrine, Well,
                         QuestTree, QuestStone, QuestGibbet, QuestTome, QuestMalus, Door };

struct MonsterDefinition {
    MonsterKind id;
    const char *token;
    float maxLife, speed, damage, attackInterval, sightRange, attackRange;
};
struct RegionDefinition {
    RegionId id;
    std::string name, mapPath;
    bool safe = false;
};
const MonsterDefinition &monsterDefinition(MonsterKind id);
} // namespace d2x
