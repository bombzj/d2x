#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/aura.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
namespace {
bool inRange(Vec origin, Vec target, float radius) {
    const float dx = std::floor(origin.x) - std::floor(target.x);
    const float dy = std::floor(origin.y) - std::floor(target.y);
    return dx * dx + dy * dy <= radius * radius;
}
float rollDamage(uint64_t &random, float minimum, float maximum) {
    const int low = int(minimum * 256.f), high = int(maximum * 256.f);
    if (high <= low) return float(low) / 256.f;
    rollRandom(random);
    return float(low + int(uint32_t(random) % unsigned(high - low))) / 256.f;
}
} // namespace
void SkillRuntime::pulseLegacyAura(EntityId actor, const AuraDefinition &aura, EffectFrame &nextFrame) {
    const auto source = combatUnit(actor);
    if (!source) return;
    nextFrame = world_.frame() + EffectFrame(aura.periodFrames);
    auto apply = [&](CombatEffectSet &effects, bool owner) {
        if (aura.state.id < 0) return std::vector<RemovedCombatEffect>{};
        for (const auto &existing : effects.entries())
            if (existing.activeAt(world_.frame()) && existing.spec.state.id == aura.state.id &&
                existing.spec.source.level > aura.rank) return std::vector<RemovedCombatEffect>{};
        CombatEffectSpec effect;
        effect.state = aura.state;
        effect.source = {CombatEffectSource::Monster, actor, aura.skill, aura.rank};
        effect.duration = EffectFrame(aura.periodFrames + 1);
        effect.modifiers = aura.modifiers;
        if (owner) effect.modifiers.combat.damagePercent += aura.ownerDamageBonus;
        return effects.apply(std::move(effect), world_.frame()).removed;
    };
    if (aura.hostile) {
        if (aura.ownerState.id >= 0) {
            CombatEffectSpec ownerEffect;
            ownerEffect.state = aura.ownerState;
            ownerEffect.source = {CombatEffectSource::Monster, actor, aura.skill, aura.rank};
            ownerEffect.duration = EffectFrame(aura.periodFrames + 1);
            source.effects->apply(std::move(ownerEffect), world_.frame());
        }
    }
    for (auto target : combatUnits()) {
        if (!target.alive() || !world_.nearby(*source.position, *target.position) || !inRange(*source.position, *target.position, aura.radius)) continue;
        if (aura.hostile ? !canAttack(actor, target.id) : relation(actor, target.id) != Relation::Allied) continue;
        if (aura.state.id >= 0) {
            const auto removed = apply(*target.effects, target.id == actor);
            if (target.player) world_.effectsChanged(removed);
        }
        if (aura.hostile && aura.element >= 0)
            dealDamage({actor, target.id, rollDamage(*source.random, aura.minimumDamage, aura.maximumDamage), MonsterDamageType(aura.element)});
    }
}
void SkillRuntime::applyNativeCurse(EntityId actor, EntityId defender, const AuraDefinition &definition) {
    const auto source = combatUnit(actor);
    if (!source) return;
    const auto target = combatUnit(defender);
    if (!target || (limitedRandom(*source.random, 4)) == 0) return;
    auto curse = [&](CombatUnit victim) {
        if (!world_.auraEligible(victim.id, false)) return;
        const int resistance = std::max(0, victim.stats.attributes.combat.curseResistance);
        if (resistance >= 100 || victim.effects->hasState(world_.attractState(), world_.frame())) return;
        for (const auto &existing : victim.effects->entries())
            if (existing.activeAt(world_.frame()) && existing.spec.state.id == definition.state.id &&
                existing.spec.source.definition == definition.skill && existing.spec.source.level > definition.rank) return;
        CombatEffectSpec effect;
        effect.state = definition.state;
        effect.source = {CombatEffectSource::Monster, actor, definition.skill, definition.rank};
        effect.stacking = EffectStacking::AuraLevel;
        effect.duration = EffectFrame(definition.periodFrames - int64_t(definition.periodFrames) * resistance / 100);
        effect.modifiers = definition.modifiers;
        const auto removed = victim.effects->apply(std::move(effect), world_.frame()).removed;
        if (victim.player) world_.effectsChanged(removed);
    };
    const float radius = std::clamp(definition.radius, 1.f, 40.f);
    for (auto victim : combatUnits())
        if (victim.alive() && canAttack(actor, victim.id) &&
            world_.nearby(*target.position, *victim.position) && inRange(*target.position, *victim.position, radius))
            curse(victim);
}
} // namespace d2x
