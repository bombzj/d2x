#pragma once
#include "gameplay/combat/missile_effects.hpp"
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include <vector>

namespace d2x {
ProjectileResource loadProjectileResource(const DataTable &missiles, size_t row, Archives &archives);
// Imports fixed-table SrvHit 1/2/3/44 effects. Skill-dependent radius/damage
// formulas must be resolved by the skill importer before entering gameplay.
MissileImpactSpec loadMissileImpact(const DataTable &missiles, size_t row, Archives &archives,
                                    std::vector<ProjectileResource> &resources);
} // namespace d2x
