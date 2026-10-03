#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include <algorithm>
#include "core/random.hpp"
#include <stdexcept>
#include <type_traits>

namespace d2x {
void SkillRuntime::triggerCombatEffects(EntityId target, CombatEffectEvent event, EntityId other) {
    const auto unit = combatUnit(target), attacker = combatUnit(other);
    if (!unit || !attacker.alive()) return;
    const auto actions = unit.effects->reactions(event, world_.frame());
    for (const auto &trigger : actions)
        std::visit([&](const auto &action) {
            if constexpr (std::is_same_v<std::decay_t<decltype(action)>, ColdMeleeRetaliation>) {
                if (!world_.hasResolver()) throw std::runtime_error("Ice armor owner has no skill resolver");
                // EventFunc03 resolves current owner damage with the armor's
                // cast rank, rolls the owner's seed and never creates a missile.
                const auto skill = world_.resolve(target, trigger.source.definition, trigger.source.level);
                const int minimum = int(skill.minimumDamage * 256.f), maximum = int(skill.maximumDamage * 256.f);
                const float damage = float(minimum + limitedRandom(*unit.random,
                    uint32_t(std::max(0, maximum - minimum)))) / 256.f;
                dealDamage({target, other, damage, MonsterDamageType::Cold,
                    missileColdDuration(target, attacker, int(skill.coldDuration * 25.f + .5f)), false, false});
                if (skill.hitOverlayId >= 0)
                    world_.addEffect({*attacker.position, 0, skill.hitOverlayDuration, -1, skill.hitOverlayId, other});
            } else if constexpr (std::is_same_v<std::decay_t<decltype(action)>, FreezeAttacker>) {
                const auto &freeze = action;
                world_.freeze(target, attacker.id, int(freeze.duration * 25.f + .5f));
                if (freeze.overlayId >= 0)
                    world_.addEffect({*attacker.position, 0, freeze.overlayDuration, -1, freeze.overlayId, other});
            }
        }, trigger.action);
}
} // namespace d2x
