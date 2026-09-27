#pragma once
#include "gameplay/combat/missile_effects.hpp"
#include <vector>

namespace d2x {
struct WeaponProjectileSpec {
    int id = -1;
    float speed = 0, lifetime = 0;
    std::string art;
    bool groundTargeted = false;
    struct DamageRange { int minimum = 0, maximum = 0; };
    std::array<DamageRange, 6> damage{}; // Native damage in 1/256 HP.
    std::optional<MissileImpactSpec> impact;
    std::vector<ProjectileResource> resources;
};
} // namespace d2x
