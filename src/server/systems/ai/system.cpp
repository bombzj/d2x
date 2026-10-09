#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/objects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "gameplay/monsters/ranged_decision.hpp"
#include "gameplay/monsters/special_decision.hpp"
#include "gameplay/monsters/boss_decision.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x::server::ai {
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    std::erase_if(state_.controllers, [&](const auto &entry) {
        const auto *actor = ports_.monsters.find(entry.first); return !actor || actor->life <= 0;
    });
    for (const auto &[id, monster] : ports_.monsters.read().actors) {
        // MPQ Idle / old monster_ai::PrisonDoor: a destructible gate never
        // pursues or attacks, including its permitted content substitute.
        if(monster.implementation==MonsterKind::PrisonDoor) {ports_.monsters.stop(id);continue;}
        if (monster.life <= 0 || monster.busyUntil > tick.tick || monster.frozenUntil > tick.tick || monster.owner || ports_.skills.busy(id,tick.tick)) continue;
        auto [entry, fresh] = state_.controllers.try_emplace(id);
        auto &controller = entry->second;
        if (fresh) controller.random = monster.combatRandom;
        if (controller.nextDecision > tick.tick) continue;
        controller.actor = id; controller.nextDecision = tick.tick + uint64_t(monster.rule.decisionTicks);
        const auto &area=ports_.areas.at(monster.area);
        bool active=false;
        for(const auto &[key,player]:ports_.players.all()) { (void)key;if(player.entered && player.area==monster.area && area.definition.activation.nearby(player.position,monster.position)) {active=true;break;} }
        if(!active || area.definition.town) {controller.pursuing=false;ports_.monsters.stop(id);continue;}
        // AiUtil::sub_6FCF2110 and DRLGROOM_CheckLOSDraw: initial acquisition
        // in rooms without LOSDraw requires a clear missile-barrier ray.
        // Native flag 8 keeps pursuit enabled once a target was acquired.
        const auto *room=area.definition.activation.room(monster.position);
        const bool requireSight=room && !room->checkLosDraw && !controller.acquiredTarget;
        auto visible=[&](Vec point) {return !requireSight || area.definition.collision.missileSegment(monster.position,point,{4,1});};
        std::optional<UnitTarget> target;Vec targetPosition;int targetSize=2;
        int distance = std::min(55, monster.rule.ai.searchDistance);
        for (const auto &[playerId, player] : ports_.players.all()) {
            (void)playerId;
            if (!player.entered || player.area != monster.area || player.persistent.player.hp <= 0 || !area.definition.activation.nearby(player.position,monster.position)) continue;
            const int candidate = monsterAiDistance(player.position, 0, monster.position);
            if (candidate < distance && visible(player.position)) { distance = candidate; target=UnitTarget{player.actor,0,0};targetPosition=player.position;targetSize=2; }
        }
        std::optional<UnitTarget> alternative;Vec alternativePosition;int alternativeSize=0;
        int alternativeDistance=monster.rule.ai.searchDistance;
        for(const auto &[petId,pet]:ports_.monsters.read().actors) if(pet.amazonPet && pet.life>0 && pet.area==monster.area && area.definition.activation.nearby(pet.position,monster.position)) {
            if(!visible(pet.position)) continue;
            const int candidate=monsterAiDistance(pet.position,0,monster.position);
            if(pet.rule.threat>1) {
                if(candidate<distance) {distance=candidate;target=UnitTarget{petId,0,1};targetPosition=pet.position;targetSize=pet.rule.size;}
            } else if(candidate<alternativeDistance) {alternativeDistance=candidate;alternative=UnitTarget{petId,0,1};alternativePosition=pet.position;alternativeSize=pet.rule.size;}
        }
        // AiUtil callback 5 separates Threat<=1 targets; sub_6FCF27B0 only
        // promotes a nearby alternative when the primary has no usable route.
        if(alternative && (!target || (alternativeDistance<=5 && area.definition.collision.path(monster.position,targetPosition,true,monster.rule.collision).empty()))) {
            distance=alternativeDistance;target=alternative;targetPosition=alternativePosition;targetSize=alternativeSize;
        }
        if (controller.target != (target ? std::optional(target->id) : std::nullopt)) {
            controller.pursuing = false; controller.charged = false; ports_.monsters.stop(id);
        }
        controller.target = target ? std::optional(target->id) : std::nullopt;
        if (!target) { controller.pursuing = false; ports_.monsters.stop(id); continue; }
        controller.acquiredTarget=true;
        if(monster.rule.opensDoors && ports_.objects.openMonsterDoor(id,targetPosition,tick.tick)) {
            ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+5;continue;
        }
        if(const auto unique=uniqueAction(id,*target,targetPosition,tick,controller)) {blocked|=*unique==StepStatus::Blocked;continue;}
        const auto &rules = monster.rule.ai;
        if(rules.kind==MonsterAiKind::FoulCrowNest) {
            if(nestFamily(id,*target,distance,tick,controller)==StepStatus::Blocked) blocked=true;
            continue;
        }
        if (hasMeleeDecision(rules.kind) || hasRangedDecision(rules.kind) || hasSpecialDecision(rules.kind) || rules.kind==MonsterAiKind::FallenShaman || hasBossDecision(rules.kind)) {
            if (familyAction(id,*target,targetPosition,targetSize,tick,controller) == StepStatus::Blocked) blocked = true;
            continue;
        }
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
