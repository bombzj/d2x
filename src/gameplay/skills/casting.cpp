#include "gameplay/skills/missile.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/world_port.hpp"
#include "core/random.hpp"
#include "gameplay/skills/runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace d2x {
void SkillRuntime::stopChannel(SkillCaster player) {
    if (!player.channel) return;
    player.channel.reset();
    player.castTime = 0;
}
void SkillRuntime::advanceSkillCasting(SkillCaster player, float dt, bool moving) {
    auto &channel = player.channel;
    if (channel && (player.dead || player.hitTime > 0 || moving)) stopChannel(player);
    if (channel && channel->enemy) {
        const auto enemy = combatUnit(channel->enemy);
        if (!enemy.alive() || !canAttack(player.id, enemy.id) || !active(*enemy.position)) stopChannel(player);
        else {
            channel->target = *enemy.position;
            const auto direction = (*enemy.position - player.pos).unit();
            if (direction.length() > 0) player.look = direction;
        }
    }
    if (channel) {
        channel->age += dt;
        channel->remaining -= dt;
        while (channel && channel->remaining < -.00001f) {
            const bool consumeMana = channel->pulses % 2 == 0;
            if (consumeMana && player.mana < channel->skill.manaCost) { stopChannel(player); break; }
            if (channel->pulses == 0)
                emit(MissileReleased{channel->skill.missileId});
            const auto pulse = *channel;
            releaseSkillCast(player, pulse.skill, pulse.target, 0, consumeMana);
            if (!channel) break;
            ++channel->pulses;
            channel->remaining += 1.f / 25.f;
        }
        if (channel) player.castTime = 1;
    }
    auto &pendingCast = player.pendingCast;
    if (pendingCast && (player.dead || player.hitTime > 0)) {
        pendingCast.reset();
        player.castTime = 0;
    }
    if (pendingCast) {
        pendingCast->remaining -= dt;
        if (pendingCast->remaining <= .00001f) {
            auto cast = *pendingCast;
            pendingCast.reset();
            if (cast.enemy) {
                if (cast.skill.summon) {
                    if (!world_.usableCorpse(cast.enemy)) return;
                    cast.target = unitPosition(cast.enemy);
                } else if (cast.skill.effect == SkillBehavior::Telekinesis) {
                    if (!world_.telekinesis(cast.enemy, cast.skill.telekinesisRange, false)) return;
                } else if (cast.skill.effect == SkillBehavior::Enchant || cast.skill.effect == SkillBehavior::HolyBolt) {
                    const auto target = combatUnit(cast.enemy);
                    if (!target.alive() || !active(*target.position))
                        cast.enemy = {};
                    else if (cast.skill.effect == SkillBehavior::HolyBolt) {
                        cast.target = *target.position;
                        player.look = (cast.target - player.pos).unit();
                    } else if (relation(player.id, target.id) != Relation::Allied) cast.enemy = {};
                } else {
                    const auto enemy = combatUnit(cast.enemy);
                    if (!enemy.alive() || !canAttack(player.id, enemy.id) || !active(*enemy.position)) return;
                    cast.target = *enemy.position;
                    const auto direction = (cast.target - player.pos).unit();
                    if (direction.length() > 0) player.look = direction;
                }
            }
            releaseSkillCast(player, cast.skill, cast.target, cast.staticFieldMinimum, true, cast.enemy);
        }
    }
}
bool SkillRuntime::beginSkillCast(SkillCaster player, const SkillCastSpec &skill, Vec target, bool teleportAllowed,
                              int staticFieldMinimum, EntityId enemy) {
    if (player.channel) {
        if (skill.sourceId == player.channel->skill.sourceId && !player.dead && player.hitTime <= 0) {
            player.channel->target = target;
            player.channel->enemy = enemy;
            const auto direction = (target - player.pos).unit();
            if (direction.length() > 0) player.look = direction;
            return true;
        }
        stopChannel(player);
    }
    if (player.dead || player.blockAnimation || player.charge || player.castTime > 0 ||
        player.meleeTime > 0 || player.hitTime > 0 || skill.castDuration <= 0)
        return false;
    if (skill.delayFrames > 0 && world_.frame() < player.skillDelayUntil) return false;
    if (skill.requiresShield && !player.shield) return false;
    if (skill.curse && skill.curse->ai == CurseAi::Attract) {
        const auto victim = combatUnit(enemy);
        if (!victim.alive() || !canAttack(player.id, enemy) || !victim.monster ||
            !world_.curseEligible(victim.id, true)) return false;
    }
    if (skill.heaven && (!combatUnit(enemy).alive() || !canAttack(player.id, enemy))) return false;
    if (skill.blizzard && !blizzardTargetClear(player.pos, target)) return false;
    if (skill.meteor && !blizzardTargetClear(player.pos, target)) return false;
    if (skill.hydraFrames > 0 && !blizzardTargetClear(player.pos, target)) return false;
    if (skill.effect == SkillBehavior::Telekinesis &&
        (!world_.telekinesis(enemy, skill.telekinesisRange, false))) return false;
    if (skill.effect == SkillBehavior::Teleport && (!teleportAllowed || !world_.walkable(player.id, target))) {
        world_.message("Teleport needs permitted, clear ground");
        return false;
    }
    if (skill.summon) {
        if (!enemy) enemy = world_.corpseNear(target);
        if (!world_.usableCorpse(enemy)) { world_.message("A usable monster corpse is required"); return false; }
        target = unitPosition(enemy);
    }
    if (player.mana < std::max(skill.manaCost, skill.startMana)) {
        world_.message("Not enough mana");
        return false;
    }
    const Vec aim = (target - player.pos).unit();
    if (aim.length() > 0) player.look = aim;
    player.castTime = skill.castDuration;
    player.lastCastDuration = player.castTime;
    player.lastCastRate = skill.castRate;
    player.lightningSequence = skill.arc.has_value();
    world_.beginCast(player.id);
    world_.message({});
    if (skill.effect == SkillBehavior::Inferno) {
        player.mana -= skill.manaCost;
        player.channel = ChannelSkillCast{skill, target, skill.castImpact, 0, 0, enemy};
        emit(SkillCast{player.id, skill.sourceId, player.pos});
        return true;
    }
    emit(SkillCast{player.id, skill.sourceId, player.pos});
    if (skill.castOverlayId >= 0)
        world_.addEffect({player.pos, 0, skill.visualDuration,
                                      -1, skill.castOverlayId, player.id});
    player.pendingCast = PendingSkillCast{skill, target, staticFieldMinimum, skill.castImpact, enemy};
    return true;
}
} // namespace d2x
