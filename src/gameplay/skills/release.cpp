#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SkillRuntime::releaseSkillCast(SkillCaster player, const SkillCastSpec &skill, Vec target,
                                     int staticFieldMinimum, bool consumeMana, EntityId targetUnit) {
    if (player.dead || (consumeMana && player.mana < skill.manaCost) ||
        (skill.effect == SkillBehavior::Teleport && !world_.walkable(player.id, target))) return;
    if (skill.summon) {
        if (world_.summonCorpse(player.id, skill, targetUnit)) {
            if (consumeMana) player.mana -= skill.manaCost;
            emit(SkillActivated{skill.sourceId});
        }
        return;
    }
    if (skill.blizzard && !blizzardTargetClear(player.pos, target)) return;
    if (skill.meteor && !blizzardTargetClear(player.pos, target)) return;
    if (skill.hydraFrames > 0) {
        if (world_.summonHydra(player.id, skill, target)) {
            if (consumeMana) player.mana -= skill.manaCost;
            player.skillDelayUntil = world_.frame() + EffectFrame(skill.delayFrames);
            emit(SkillActivated{skill.sourceId});
        }
        return;
    }
    if (skill.requiresShield && !player.shield) return;
    if (skill.curse && skill.curse->ai == CurseAi::Attract) {
        const auto victim = combatUnit(targetUnit);
        if (!victim.alive() || !canAttack(player.id, targetUnit) || !victim.monster ||
            !world_.curseEligible(victim.id, true) ||
            !world_.auraEligible(victim.id, false) || victim.stats.attributes.combat.curseResistance >= 100 ||
            victim.effects->hasState(world_.attractState(), world_.frame())) return;
    }
    if (skill.heaven && (!combatUnit(targetUnit).alive() || !canAttack(player.id, targetUnit))) return;
    if (consumeMana) player.mana -= skill.manaCost;
    if (skill.delayFrames > 0)
        player.skillDelayUntil = world_.frame() + EffectFrame(skill.delayFrames);
    if (skill.missileId >= 0 && skill.effect != SkillBehavior::Inferno && !skill.appliedEffect)
        emit(MissileReleased{skill.missileId});
    if (skill.curse) {
        releaseCurse(player, skill, target, targetUnit);
    } else if (skill.effect == SkillBehavior::Telekinesis) {
        releaseTelekinesis(player, skill, target, targetUnit);
    } else if (skill.appliedEffect) {
        releaseAppliedEffect(player, skill, target, targetUnit);
    } else if (skill.effect == SkillBehavior::Teleport) {
        releaseTeleport(player, skill, target, targetUnit);
    } else if (skill.effect == SkillBehavior::StaticField) {
        releaseStaticField(player, skill, staticFieldMinimum);
    } else {
        launchProjectiles(player, skill, target, targetUnit);
    }
}
} // namespace d2x
