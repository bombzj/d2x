#include "gameplay/skills/missile.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include <stdexcept>
#include <utility>

namespace d2x {
SkillRuntime::SkillRuntime(ISkillWorld &world, ISkillWeaponWorld &weapons) : world_(world), weapons_(weapons) {}
CombatUnit SkillRuntime::combatUnit(EntityId id) const { return world_.unit(id); }
std::vector<CombatUnit> SkillRuntime::combatUnits() const { return world_.units(); }
Vec SkillRuntime::unitPosition(EntityId id) const { return world_.position(id); }
bool SkillRuntime::canAttack(EntityId actor, EntityId target) const { return world_.canAttack(actor, target); }
Relation SkillRuntime::relation(EntityId first, EntityId second) const { return world_.relation(first, second); }
bool SkillRuntime::active(Vec point) const { return world_.active(point); }
float SkillRuntime::dealDamage(const DamageRequest &hit) { return world_.damage(hit); }
void SkillRuntime::emit(SkillEvent event) { world_.emit(std::move(event)); }
float SkillRuntime::missileColdDuration(EntityId actor, const CombatUnit &target, int frames) const {
    return world_.coldDuration(actor, target, frames);
}
Missile &SkillRuntime::launchStraight(Missile prepared) {
    if (prepared.id || prepared.combatRandom)
        throw std::invalid_argument("Straight missile requires an unallocated payload");
    prepared.id = world_.allocate();
    auto &missile = world_.addMissile(std::move(prepared));
    missile.combatRandom = world_.childSeed();
    return missile;
}
} // namespace d2x
