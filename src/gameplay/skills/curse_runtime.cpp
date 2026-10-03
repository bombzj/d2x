#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SkillRuntime::releaseCurse(SkillCaster player, const SkillCastSpec &skill, Vec target, EntityId targetUnit) {
    for (auto defender : combatUnits()) {
        if (!defender.alive() || !canAttack(player.id, defender.id) || !active(*defender.position) ||
            defender.stats.collisionSize <= 0 || defender.stats.attributes.combat.curseResistance >= 100) continue;
        if (!world_.curseEligible(defender.id, skill.curse->ai != CurseAi::None)) continue;
        if (!world_.auraEligible(defender.id, false)) continue;
        if (defender.effects->hasState(world_.attractState(), world_.frame())) continue;
        if (skill.curse->ai == CurseAi::DimVision && std::any_of(defender.effects->entries().begin(), defender.effects->entries().end(),
            [this](const auto &effect) { return effect.activeAt(world_.frame()) && effect.spec.curseAi == CurseAi::Confuse; })) continue;
        const int deltaX = int(defender.position->x) - int(target.x), deltaY = int(defender.position->y) - int(target.y);
        if (deltaX * deltaX + deltaY * deltaY > skill.curse->radius * skill.curse->radius) continue;
        if (skill.curse->ai == CurseAi::Attract && defender.id != targetUnit) {
            if (defender.monster)
                world_.attract(defender.id, targetUnit, world_.frame() +
                    EffectFrame(std::max(1, skill.curse->frames / world_.aiCurseDivisor())));
            continue;
        }
        CombatEffectSpec curse;
        curse.state = skill.curse->state;
        curse.source = {CombatEffectSource::Skill, player.id, skill.sourceId, skill.rank};
        curse.duration = EffectFrame(std::max(1, skill.curse->frames));
        if (skill.curse->ai != CurseAi::None) curse.duration = EffectFrame(std::max(1, skill.curse->frames / world_.aiCurseDivisor()));
        curse.duration = EffectFrame(std::max<int64_t>(1, int64_t(*curse.duration) *
            std::clamp(100 - defender.stats.attributes.combat.curseResistance, 0, 200) / 100));
        curse.curseAi = skill.curse->ai;
        curse.curseCenter = target;
        curse.modifiers = skill.curse->modifiers;
        curse.lifeTapOverlay = skill.curse->healOverlay;
        curse.lifeTapOverlayDuration = skill.curse->healOverlayDuration;
        if (defender.stats.monsterResistanceRules && defender.identity.role != CombatRole::Hireling) {
            auto base = defender.stats.attributes.combat.physicalResist;
            for (const auto &effect : defender.effects->entries())
                if (effect.activeAt(world_.frame())) base -= effect.spec.modifiers.combat.physicalResist;
            if (base >= 100) curse.modifiers.combat.physicalResist /= 5;
            for (const auto type : {MonsterDamageType::Fire, MonsterDamageType::Cold, MonsterDamageType::Lightning, MonsterDamageType::Poison}) {
                int original = rawResistance(defender.stats.attributes, type);
                for (const auto &effect : defender.effects->entries()) {
                    if (!effect.activeAt(world_.frame())) continue;
                    const auto &modifiers = effect.spec.modifiers;
                    original -= type == MonsterDamageType::Fire ? modifiers.fireResist : type == MonsterDamageType::Cold ?
                        modifiers.coldResist : type == MonsterDamageType::Lightning ? modifiers.lightningResist : modifiers.poisonResist;
                }
                auto &amount = type == MonsterDamageType::Fire ? curse.modifiers.fireResist : type == MonsterDamageType::Cold ?
                    curse.modifiers.coldResist : type == MonsterDamageType::Lightning ? curse.modifiers.lightningResist : curse.modifiers.poisonResist;
                if (original >= 100 && amount < 0) amount /= 5;
            }
        }
        world_.effectsChanged(defender.effects->apply(std::move(curse), world_.frame()).removed);
        if (skill.curse->ai != CurseAi::None && defender.monster) {
            world_.resetCurseAi(defender.id);
        }
    }
    emit(SkillActivated{skill.sourceId});
}
} // namespace d2x
