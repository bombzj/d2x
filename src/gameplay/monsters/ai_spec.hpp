#pragma once
#include <array>

namespace d2x {
enum class MonsterAiKind { Skeleton, Brute, Zombie, Fallen, CorruptRogue, Goatman, QuillRat,
                           Wraith, CorruptLancer, CorruptArcher, SkeletonBow, Bighead,
                           SkeletonMage, Fetish, Vampire, FallenShaman, FoulCrowNest, BloodHawk,
                           Arach, Smith, Griswold, BloodRaven, Countess, Andariel, GargoyleTrap };
struct MonsterAiProfile {
    MonsterAiKind kind;
    std::array<int, 8> params{};
    int meleeRange = 0; // Resolved MonStats2.MeleeRng, including the 255 weapon-class sentinel.
    int retreatVelocityBonus = 0;
    int searchDistance = 35; // AiUtil::sub_6FCF2110: zero AiDist uses 35.
};
} // namespace d2x
