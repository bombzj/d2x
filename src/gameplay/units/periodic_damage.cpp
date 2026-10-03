#include "gameplay/units/periodic_damage.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/combat/damage_request.hpp"
#include <algorithm>

namespace d2x {
void advancePeriodicDamage(const CombatUnit &unit, float dt, bool poisonCannotKill,
                           const PeriodicDamageHandler &applyDamage) {
    if (*unit.poison.remaining > 0) {
        const float elapsed = std::min(dt, *unit.poison.remaining);
        *unit.poison.remaining -= elapsed;
        float amount = *unit.poison.rate * elapsed;
        if (poisonCannotKill) amount = std::min(amount, std::max(0.f, *unit.life - 1.f));
        applyDamage({*unit.poison.source, unit.id, amount, MonsterDamageType::Poison,
                     0, true, false, false, DamagePermission::ExistingEffect});
        // Removal follows damage delivery, including its original death callbacks.
        if (*unit.poison.remaining <= 0) *unit.poison.rate = 0;
    }
    if (*unit.openWounds.remaining > 0) {
        const float elapsed = std::min(dt, *unit.openWounds.remaining);
        *unit.openWounds.remaining -= elapsed;
        applyDamage({*unit.openWounds.source, unit.id, *unit.openWounds.rate * elapsed,
                     MonsterDamageType::Physical, 0, true, false, false, DamagePermission::ExistingEffect});
        if (*unit.openWounds.remaining <= 0) *unit.openWounds.rate = 0;
    }
}
} // namespace d2x
