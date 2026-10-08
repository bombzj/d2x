#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/monsters/ranged_decision.hpp"
#include "gameplay/monsters/special_decision.hpp"
#include "gameplay/monsters/shaman_decision.hpp"
#include "gameplay/monsters/boss_decision.hpp"
#include <algorithm>
namespace d2x::server::ai {
StepStatus System::familyAction(EntityId id, UnitTarget target, Vec position, int size, TickContext tick, Controller &controller) {
    const auto &monster = *ports_.monsters.find(id);
    const auto &area = ports_.areas.at(monster.area);
    const auto distance = meleeDistance(monster.position, monster.rule.size, position, size);
    const bool clear = area.definition.collision.missileSegment(monster.position,position,{4,1});
    auto move = [&](int stop, int percentage, bool running) {
        return ports_.monsters.requestMove({id, {monster.area, area.generation, position}, target.id, stop, percentage, running});
    };
    if (monster.rule.ai.kind == MonsterAiKind::Fallen) {
        const monsters::Actor *corpse = nullptr;
        for (const auto &[key, other] : ports_.monsters.read().actors) {
            (void)key;
            if (other.area == monster.area && other.life <= 0 && other.deathOccurrence > controller.observedDeath && tick.tick < other.busyUntil &&
                monsterAiDistance(other.position, 0, monster.position) < 15 && (!corpse || other.deathOccurrence > corpse->deathOccurrence)) corpse = &other;
        }
        if (corpse) {
            const Vec goal = monsterRetreatPoint(monster.position, position, 12);
            controller.pursuing = bool(ports_.monsters.requestMove({id, {monster.area, area.generation, goal}, {}, 0, 125, false}));
            controller.commanded = false; controller.alerted = true; controller.observedDeath = corpse->deathOccurrence;
            controller.nextDecision = tick.tick + 1;
            if (controller.pursuing) return StepStatus::Complete;
        }
    }
    // Continue an accepted WL/RN action without rerolling its movement choice.
    // Repath at the old 0.7-second cadence, and finish its own arrival threshold.
    if (controller.pursuing && !monster.route.empty() && (!monster.movementTarget || monster.movementTarget == target.id) &&
        (!monster.movementTarget || !(distance <= monster.stopDistance && clear))) {
        if (monster.movementTarget && tick.tick >= controller.nextPath) {
            const auto result = move(monster.stopDistance, monster.velocityPercent, monster.running);
            controller.nextPath = tick.tick + 18;
            if (!result) { ports_.monsters.stop(id); controller.pursuing = false; }
        }
        controller.nextDecision = tick.tick + 1;
        return StepStatus::Complete;
    }
    controller.pursuing = false; ports_.monsters.stop(id);
    int targetLife = 100;
    if (const auto *pet = ports_.monsters.find(target.id)) targetLife = int(pet->life * 100 / pet->maximumLife);
    else for (const auto &[key, player] : ports_.players.all()) { (void)key; if (player.actor == target.id) targetLife = int(player.persistent.player.hp * 100 / player.totals.character.maxLife); }
    MonsterDecisionInput input
        {distance <= monster.rule.meleeRange && clear, controller.charged,
         monsterAiDistance(position, target.type==0?0:size, monster.position), monster.rule.difficulty, controller.random,
         monster.hitOccurrence != controller.observedHit, int(monster.area) == 17,
         int(monster.life * 100 / monster.maximumLife), controller.commanded, controller.alerted,
         monster.identity.ownerSpawnKey == monster.identity.spawnKey, controller.phase, controller.loop, targetLife, monster.webUntil>tick.tick, clear};
    input.trapAxisAligned=std::abs(int(std::floor(position.x))-int(std::floor(monster.position.x)))<6 || std::abs(int(std::floor(position.y))-int(std::floor(monster.position.y)))<6;
    if(monster.rule.ai.kind==MonsterAiKind::CorruptArcher || monster.rule.ai.kind==MonsterAiKind::Vampire) {
        const int radius=monster.rule.ai.kind==MonsterAiKind::CorruptArcher?12:8;
        input.retreatBlocked=area.definition.collision.path(monster.position,monsterRetreatPoint(monster.position,position,radius),true,monster.rule.collision).empty();
    }
    std::optional<UnitTarget> corpse;
    if(monster.rule.ai.kind==MonsterAiKind::FallenShaman) for(const auto &[key,other]:ports_.monsters.read().actors) {
        (void)other;if(ports_.monsters.resurrectionTarget(id,key,tick.tick)) corpse=UnitTarget{key,0,1};
    }
    std::optional<MonsterBossDecision> boss;
    int firewallPhase=controller.phase;
    if(monster.rule.ai.kind==MonsterAiKind::Countess && firewallPhase>=int(monster.skillPositions.size()) && tick.tick-controller.firewallCycle>700) firewallPhase=0;
    if(hasBossDecision(monster.rule.ai.kind)) {
        const auto *homeRoom=area.definition.activation.room(monster.home);
        const auto *targetRoom=area.definition.activation.room(position);
        boss=decideBossMonster(monster.rule.ai,{input,monsterAiDistance(monster.home,monster.rule.size,monster.position),monsterAiDistance(monster.home,0,position),controller.summonChance,homeRoom==targetRoom,firewallPhase<int(monster.skillPositions.size())});
    }
    const auto decision = boss?std::optional{boss->action}:monster.rule.ai.kind==MonsterAiKind::FallenShaman?std::optional{decideFallenShaman(monster.rule.ai,input,bool(corpse))}:
        hasRangedDecision(monster.rule.ai.kind)?decideRangedMonster(monster.rule.ai,input):hasSpecialDecision(monster.rule.ai.kind)?decideSpecialMonster(monster.rule.ai,input):decideMeleeMonster(monster.rule.ai,input);
    if (!decision) return StepStatus::NotImplemented;
    if (decision->action == MonsterDecisionAction::Shout) {
        const auto result = ports_.skills.requestCast({id, 0, target, tick.tick, 9});
        if (result.status == DomainStatus::Capacity) { controller.nextDecision = tick.tick + 1; return StepStatus::Blocked; }
        if(result && monster.identity.ownerSpawnKey==monster.identity.spawnKey) {commandParty(monster);controller.commanded=true;}
    } else if (decision->action == MonsterDecisionAction::Attack || decision->action == MonsterDecisionAction::Special) {
        const auto skill=decision->action==MonsterDecisionAction::Special?monster.rule.skillIds.at(size_t(decision->skillSlot)):uint16_t(0);
        const auto actualTarget=monster.rule.ai.kind==MonsterAiKind::FallenShaman && decision->skillSlot==0?*corpse:target;
                std::optional<Vec> point;
        if(boss && monster.rule.ai.kind==MonsterAiKind::Countess && decision->skillSlot==0) point=monster.skillPositions.at(size_t(firewallPhase));
        if(boss && monster.rule.ai.kind==MonsterAiKind::BloodRaven && decision->skillSlot==0) point=Vec{std::floor(position.x)+.5f,std::floor(position.y)+.5f}+boss->summonOffset;
        const auto result = ports_.skills.requestCast({id,skill,actualTarget,tick.tick,decision->attackMode,point});
        if (result.status == DomainStatus::Capacity) {
            controller.nextDecision = tick.tick + 1;
            return StepStatus::Blocked; // Preserve the chosen roll and charge through output pressure.
        }
        if (!result) { controller.random=decision->random;controller.nextDecision = tick.tick + 1; return StepStatus::Complete; }
        if(boss && monster.rule.ai.kind==MonsterAiKind::Countess && decision->skillSlot==0) {firewallPhase++;controller.firewallCycle=tick.tick;}
    } else if (decision->action == MonsterDecisionAction::Circle || decision->action == MonsterDecisionAction::Retreat || decision->action == MonsterDecisionAction::Wander) {
        auto random = decision->random;
        auto freeMove = [&](Vec goal) { return ports_.monsters.requestMove({id, {monster.area, area.generation, goal}, {}, 0, decision->velocityPercent, decision->running}); };
        if (decision->action == MonsterDecisionAction::Circle) {
            for (const Vec goal : monsterCirclePoints(monster.position, position, decision->stopDistance, random)) if (freeMove(goal)) { controller.pursuing = true; break; }
        } else {
            const Vec goal = decision->action == MonsterDecisionAction::Retreat ? monsterRetreatPoint(monster.position, position, decision->stopDistance) : monsterWanderPoint(monster.position, decision->stopDistance, random);
            controller.pursuing = bool(freeMove(goal));
        }
        if (!controller.pursuing && monster.rule.ai.kind == MonsterAiKind::Fetish && decision->action == MonsterDecisionAction::Retreat) {
            controller.random = random; controller.observedHit = monster.hitOccurrence;
            controller.phase = controller.loop = 0; controller.nextDecision = tick.tick + 10; return StepStatus::Complete;
        }
        if (!controller.pursuing && decision->attackWhenMoveFails) {
            const auto result = ports_.skills.requestCast({id, 0, target, tick.tick,decision->failedMoveAttackMode});
            if (result.status == DomainStatus::Capacity) {controller.nextDecision = tick.tick + 1; return StepStatus::Blocked; }
        }
        if(!controller.pursuing && decision->failedMoveWanderRadius) controller.pursuing=bool(freeMove(monsterWanderPoint(monster.position,decision->failedMoveWanderRadius,random)));
        controller.random=random;controller.observedHit=monster.hitOccurrence;
        controller.charged=decision->charged;controller.alerted=decision->alerted;
        if(boss) controller.summonChance=boss->summonChance;
        controller.phase=decision->phase;controller.loop=decision->loop;
        if(decision->commandParty) commandParty(monster);
        controller.nextDecision = tick.tick + uint64_t(controller.pursuing ? 1 : std::max(1, decision->waitFrames));
        return StepStatus::Complete;
    } else if (decision->action == MonsterDecisionAction::Approach) {
        if (!(distance <= decision->stopDistance && clear)) {
            if (decision->approachRadius) {
                const Vec goal = monsterRadiusApproachPoint(monster.position, monster.rule.size, position, decision->approachRadius,decision->targetDistance);
                controller.pursuing = bool(ports_.monsters.requestMove({id, {monster.area, area.generation, goal}, {}, 0, decision->velocityPercent, decision->running}));
            } else if(boss && boss->returnHome) controller.pursuing=bool(ports_.monsters.requestMove({id,{monster.area,area.generation,monster.home},{},0,decision->velocityPercent,decision->running}));
            else controller.pursuing = bool(move(decision->stopDistance, decision->velocityPercent, decision->running));
            if(!controller.pursuing && monster.rule.ai.kind==MonsterAiKind::Fallen) controller.commanded=false;
            controller.nextPath = tick.tick + 18;
        }
    }
    controller.random = decision->random; controller.charged = decision->charged;
    controller.observedHit = monster.hitOccurrence;
    controller.alerted = decision->alerted;
    controller.phase = monster.rule.ai.kind==MonsterAiKind::Countess?firewallPhase:decision->phase; controller.loop = decision->loop;
    if(boss) controller.summonChance=boss->summonChance;
    if(decision->commandParty) commandParty(monster);
    controller.nextDecision = tick.tick + uint64_t(std::max(1, decision->waitFrames));
    return StepStatus::Complete;
}
void System::commandParty(const monsters::Actor &leader) {
    // Include dormant actors as well, so a command cannot disappear simply
    // because a minion has not had its first active-room AI tick yet.
    for(const auto &[id,minion]:ports_.monsters.read().actors) if(minion.life>0 && minion.area==leader.area && minion.identity.ownerSpawnKey==leader.identity.spawnKey) {
        auto [entry,fresh]=state_.controllers.try_emplace(id);auto &controller=entry->second;
        if(fresh) {controller.random=minion.combatRandom;controller.actor=id;}
        controller.commanded=true;
    }
}
}
