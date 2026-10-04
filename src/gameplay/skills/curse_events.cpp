#include "gameplay/combat/damage_request.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/skills/runtime.hpp"

namespace d2x {
void SkillRuntime::reflectIronMaiden(EntityId attacker, EntityId defender, float physicalDamage) {
    const auto source = combatUnit(attacker), target = combatUnit(defender);
    if (!source.alive() || !target.alive() || physicalDamage <= 0) return;
    for (const auto &event : source.effects->reactions(CombatEffectEvent::DealtMeleeDamage, world_.frame())) {
        const auto *reaction = std::get_if<IronMaidenRetaliation>(&event.action);
        if (!reaction) continue;
        const int percent = source.player || source.hireling ? reaction->reducedPercent : reaction->percent;
        if (percent <= 0) continue;
        DamageRequest hit{defender, attacker, float(int64_t(physicalDamage * 256.f) * percent / 100) / 256.f,
            MonsterDamageType::Physical, 0, false, false};
        hit.softHit = true;
        hit.hitClass = reaction->hitClass;
        hit.permission = DamagePermission::ExistingEffect;
        dealDamage(hit);
    }
}
void SkillRuntime::healLifeTap(EntityId attacker, EntityId defender, float physicalDamage, bool missile) {
    const auto source = combatUnit(attacker), target = combatUnit(defender);
    if (!source.alive() || !target.alive() || physicalDamage <= 0) return;
    const auto event = missile ? CombatEffectEvent::HitByMissile : CombatEffectEvent::DamagedInMelee;
    for (const auto &trigger : target.effects->reactions(event, world_.frame())) {
        const auto *healing = std::get_if<LifeTapHealing>(&trigger.action);
        if (!healing || healing->percent <= 0) continue;
        world_.restore(attacker, float(int64_t(physicalDamage * 256.f) * healing->percent / 100) / 256.f);
        if (healing->overlayId >= 0)
            world_.addEffect({*source.position, 0, healing->overlayDuration, -1, healing->overlayId, attacker});
    }
}
} // namespace d2x
