#pragma once
#include "gameplay/combat/open_wounds.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include <optional>
namespace d2x::server::monsters {struct Actor;}
namespace d2x::server::combat {
struct HirelingHitEffects {int64_t crushing{},healing{};std::optional<OpenWoundsApplication> wound;};
int hirelingDamagePercent(const monsters::Actor &source,const monsters::Actor &target);
HirelingHitEffects hirelingHitEffects(const WeaponSkillDamage &attack,const monsters::Actor &target,
    EntityId source,EntityId credit,int physicalResistance,int64_t physical,int64_t total,uint64_t tick);
}
