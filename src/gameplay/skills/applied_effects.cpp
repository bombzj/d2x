#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SkillRuntime::releaseAppliedEffect(SkillCaster player, const SkillCastSpec &skill, Vec, EntityId targetUnit) {
    auto effect = *skill.appliedEffect;
    effect.source.entity = player.id;
    auto recipient = skill.effect == SkillBehavior::Enchant ? combatUnit(targetUnit) : combatUnit(player.id);
    if (!recipient.alive() || relation(player.id, recipient.id) != Relation::Allied) recipient = combatUnit(player.id);
    const auto applied = recipient.effects->apply(std::move(effect), world_.frame());
    world_.effectsChanged(applied.removed);
    if (skill.effect == SkillBehavior::ThunderStorm) {
        const auto period = EffectFrame(skill.stormPeriod);
        player.thunderStorm = ThunderStormRuntime{applied.handle,
            ((world_.frame() + period - 1) / period) * period + 1, {}};
    }
    emit(SkillActivated{skill.sourceId});
}
} // namespace d2x
