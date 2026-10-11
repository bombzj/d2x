#pragma once
#include <array>

namespace d2x {
struct NecromancerPassiveSpec {
    enum class Kind { SkeletonMastery, GolemMastery, SummonResist };
    Kind kind = Kind::SkeletonMastery;
    std::array<int, 6> parameters{};
};
struct NecromancerPassiveValues {
    int skeletonLife = 0, skeletonDamage = 0, reviveLife = 0, reviveDamage = 0;
    int golemLife = 0, golemRating = 0, golemSpeed = 0, resistance = 0;
};
NecromancerPassiveValues evaluateNecromancerPassive(const NecromancerPassiveSpec &, int rank);
}
