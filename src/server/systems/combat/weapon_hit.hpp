#pragma once
#include "gameplay/combat/open_wounds.hpp"
#include "gameplay/combat/weapon_hit.hpp"
#include <optional>

namespace d2x { struct WeaponSkillDamage; }
namespace d2x::server::monsters { struct Actor; }
namespace d2x::server::combat {
struct WeaponHitEffects { int64_t crushing{}, healing{}, mana{}; std::optional<OpenWoundsApplication> wound; };
int hirelingDamagePercent(const monsters::Actor &source, const monsters::Actor &target);
WeaponHitEffects weaponHitEffects(const WeaponSkillDamage &, const monsters::Actor &, EntityId source,
    EntityId credit, int physicalResistance, int64_t physical, LeechDivisors, uint64_t tick, uint64_t &random);
}
