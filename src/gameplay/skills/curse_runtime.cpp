#include "gameplay/skills/curse_resolve.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include <algorithm>

namespace d2x {
bool SkillRuntime::validAttractTarget(EntityId actor, EntityId target) const {
    const auto victim = combatUnit(target);
    return victim.alive() && victim.monster && canAttack(actor, target) && active(*victim.position) &&
        world_.curseEligible(target, true) && world_.auraEligible(target, false) &&
        victim.stats.attributes.combat.curseResistance < 100 && !victim.effects->hasState(world_.attractState(), world_.frame()) &&
        std::none_of(victim.effects->entries().begin(), victim.effects->entries().end(),
            [this](const auto &effect) { return effect.activeAt(world_.frame()) && effect.spec.curseAi == CurseAi::Confuse; });
}
void SkillRuntime::releaseCurse(SkillCaster actor, const SkillCastSpec &skill, Vec target, EntityId targetUnit) {
    for (auto defender : combatUnits()) {
        if (!(skill.curse->targetFilter & (defender.player ? 1 : 2))) continue;
        if (!defender.alive() || !canAttack(actor.id, defender.id) || !active(*defender.position) ||
            defender.stats.collisionSize <= 0) continue;
        if (!world_.curseEligible(defender.id, skill.curse->ai != CurseAi::None)) continue;
        if (!world_.auraEligible(defender.id, false)) continue;
        if (defender.effects->hasState(world_.attractState(), world_.frame())) continue;
        if ((skill.curse->ai == CurseAi::DimVision || skill.curse->ai == CurseAi::Attract) &&
            std::any_of(defender.effects->entries().begin(), defender.effects->entries().end(),
            [this](const auto &effect) { return effect.activeAt(world_.frame()) && effect.spec.curseAi == CurseAi::Confuse; })) continue;
        const int deltaX = int(defender.position->x) - int(target.x), deltaY = int(defender.position->y) - int(target.y);
        if (deltaX * deltaX + deltaY * deltaY > skill.curse->radius * skill.curse->radius) continue;
        if (skill.curse->ai == CurseAi::Attract && defender.id != targetUnit) {
            if (defender.monster)
                world_.attract(defender.id, targetUnit, world_.frame() +
                    EffectFrame(std::max(1, skill.curse->frames / world_.aiCurseDivisor())),
                    {CombatEffectSource::Skill, actor.id, skill.sourceId, skill.rank});
            continue;
        }
        if (defender.stats.attributes.combat.curseResistance >= 100) continue;
        CombatEffectSpec curse;
        curse.state = skill.curse->state;
        curse.source = {CombatEffectSource::Skill, actor.id, skill.sourceId, skill.rank};
        curse.stacking = EffectStacking::CurseLevel;
        curse.duration = EffectFrame(std::max(1, skill.curse->frames));
        if (skill.curse->ai != CurseAi::None) curse.duration = EffectFrame(std::max(1, skill.curse->frames / world_.aiCurseDivisor()));
        const auto duration = int64_t(*curse.duration);
        curse.duration = EffectFrame(std::max<int64_t>(1, duration -
            duration * defender.stats.attributes.combat.curseResistance / 100));
        curse.curseAi = skill.curse->ai;
        curse.modifiers = skill.curse->modifiers;
        if (curse.modifiers.combat.ironMaidenPercent > 0)
            curse.reactions.push_back({CombatEffectEvent::DealtMeleeDamage,
                IronMaidenRetaliation{curse.modifiers.combat.ironMaidenPercent,
                    curse.modifiers.combat.ironMaidenPercent / skill.curse->reflectReducedDivisor,
                    skill.curse->reflectHitClass}});
        if (curse.modifiers.combat.lifeTapPercent > 0) {
            const LifeTapHealing healing{curse.modifiers.combat.lifeTapPercent,
                skill.curse->healOverlay, skill.curse->healOverlayDuration};
            curse.reactions.push_back({CombatEffectEvent::DamagedInMelee, healing});
            curse.reactions.push_back({CombatEffectEvent::HitByMissile, healing});
        }
        curse.modifiers = evaluateCurseModifiers(curse.modifiers, defender.stats.attributes,
            defender.effects->entries(), world_.frame(), defender.stats.monsterResistanceRules && !defender.hireling);
        const auto applied = defender.effects->apply(std::move(curse), world_.frame());
        if (defender.player && applied.accepted) world_.effectsChanged(applied.removed);
        if (applied.accepted && skill.curse->ai != CurseAi::None && defender.monster) {
            world_.resetCurseAi(defender.id);
        }
    }
    emit(SkillActivated{skill.sourceId});
}
} // namespace d2x
