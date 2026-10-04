#pragma once

namespace d2x {
enum class MonsterKind { Fallen, Zombie, Skeleton, CorruptRogue, Brute, Goatman, QuillRat,
                         Wraith, CorruptLancer, CorruptArcher, SkeletonBow, Bighead,
                         HellBovine, SkeletonMage, Fetish, Vampire, FallenShaman,
                         FoulCrowNest, BloodHawk, Arach, NecroSkeleton, Smith, Griswold, BloodRaven, Andariel, Hydra1, Hydra2, Hydra3, PrisonDoor, BoneWall, Count };
struct MonsterDefinition {
    MonsterKind id;
    const char *token;
    float maxLife, speed, damage, attackInterval, sightRange, attackRange;
};
const MonsterDefinition &monsterDefinition(MonsterKind id);
} // namespace d2x
