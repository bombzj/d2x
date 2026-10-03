#pragma once

namespace d2x {
enum class DamageType { Physical, Magic, Fire, Lightning, Cold, Poison };
// Compatibility name for existing monster and combat consumers.
using MonsterDamageType = DamageType;
} // namespace d2x
