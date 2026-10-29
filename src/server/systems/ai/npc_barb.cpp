#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/units/distance.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace d2x::server::ai {
StepStatus System::cagedWussieFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    controller.pursuing=false;controller.nextDecision=0;
    const auto portal=ports_.quests.prisonerPortal(id);
    if(!portal) {
        std::optional<PlayerId> selected;
        if(const auto *room=area.definition.activation.room(unit.position)) {
            const auto index=size_t(room-area.definition.activation.rooms().data());
            for(const auto near:area.definition.roomNear.at(index)) {
                for(const auto candidate:ports_.world.nativeRoomClients(near)) {
                    const auto *player=ports_.players.find(candidate);if(!player || !player->entered) continue;
                    const auto &playerArea=ports_.areas.at(player->area);
                    const auto point=player->position+playerArea.definition.origin-area.definition.origin;
                    if(monsterAiDistance(unit.position,unit.rule.size,point)>15) continue;
                    const auto npc=std::find_if(area.definition.npcs.begin(),area.definition.npcs.end(),[&](const auto &entry){return entry.id==id;});
                    if(npc!=area.definition.npcs.end() && npc->rule.interactable &&
                        !ports_.quests.dialogue({candidate,player->actor,player->area,playerArea.generation,0,tick.tick},npc->rule)) continue;
                    selected=candidate;break;
                }
                if(selected) break;
            }
        }
        if(!selected) {ports_.monsters.neutralMode(id);controller.nextDecision=tick.tick+25;return StepStatus::Complete;}
        const auto applied=perform(controller,[this,id,player=*selected,tick,&controller,phase=0]() mutable -> DomainResult<> {
            if(phase==0) {
                const auto activated=ports_.quests.activatePrisoner(id,player,tick.tick);
                if(!activated) return {activated.status,{}};
                if(*activated.value) phase=1;
                else {
                    ports_.quests.prisonerActivated();
                    const auto *live=ports_.monsters.find(id);if(!live) return {DomainStatus::Stale,{}};
                    auto random=live->combatRandom;const bool alert=rollRandom(random)%1000<100;
                    const auto consumed=ports_.monsters.consumeRandom(id,live->combatRandom,random);if(!consumed) return consumed;
                    phase=alert?2:3;
                }
            }
            if(phase==1) {
                const auto registered=ports_.monsters.registerWanderTarget(id);if(!registered) return registered;
                phase=3;
            }
            if(phase==2) {
                const auto *live=ports_.monsters.find(id);if(!live) return {DomainStatus::Stale,{}};
                const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Player,player,live->area},{QuestAlertFact{id,live->area,1}}});
                if(!sent) return {sent.status,{}};
                phase=3;
            }
            ports_.monsters.neutralMode(id);controller.nextDecision=tick.tick+25;
            return {DomainStatus::Applied,std::monostate{}};
        });
        return isDeferred(applied.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    const auto point=portal->position+ports_.areas.at(portal->area).definition.origin-area.definition.origin;
    // Retail 1.13c A7A51/A7A59 read Y, then X. D2MOO's X,X is a
    // transcription error. CheckWussieCounters tests any group exit/death.
    if(portal->anyResolved || unitCoordinateDistance(int(std::floor(unit.position.x)),int(std::floor(unit.position.y)),
        int(std::floor(point.x)),int(std::floor(point.y)))<4) {
        const auto removed=perform(controller,[this,id]{return ports_.quests.finishPrisonerEscape(id);});
        return isDeferred(removed.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    const bool toward=rollRandom(controller.random)%100<70;
    const bool wander=!toward && rollRandom(controller.random)%100<10;
    if(!toward && !wander) {ports_.monsters.neutralMode(id);controller.nextDecision=tick.tick+10;return StepStatus::Complete;}
    // Keep the fallback phase and its four WalkClose draws in the retained
    // operation. A full outbox/capacity retry cannot repeat the first path.
    const auto moved=perform(controller,[this,id,target=portal->object,point,tick,&controller,phase=toward?0:2,destination=Vec{}]() mutable -> DomainResult<> {
        const auto *live=ports_.monsters.find(id);if(!live) return {DomainStatus::Stale,{}};
        if(phase==0) {
            const auto result=ports_.monsters.requestMove({id,{live->area,ports_.areas.at(live->area).generation,point},target,0,75,false,1});
            if(isDeferred(result.status)) return result;
            if(result) {controller.pursuing=true;controller.nextDecision=tick.tick+1;return result;}
            phase=1;
        }
        if(phase==1) {
            // MoveToTarget flags7: delete AITHINK, then 70% WalkClose,
            // otherwise NU10 (the comparison is >=70 for the idle branch).
            auto random=live->combatRandom;const bool idle=rollRandom(random)%100>=70;
            const auto consumed=ports_.monsters.consumeRandom(id,live->combatRandom,random);if(!consumed) return consumed;
            if(idle) {ports_.monsters.neutralMode(id);controller.nextDecision=tick.tick+10;return {DomainStatus::Applied,std::monostate{}};}
            phase=2;
        }
        if(phase==2) {
            auto random=live->combatRandom;destination=monsterWanderPoint(live->position,4,random);
            const auto consumed=ports_.monsters.consumeRandom(id,live->combatRandom,random);if(!consumed) return consumed;
            phase=3;
        }
        const auto result=ports_.monsters.requestMove({id,{live->area,ports_.areas.at(live->area).generation,destination},{},0,75,false,1});
        if(result) {controller.pursuing=true;controller.nextDecision=tick.tick+1;}
        else if(!isDeferred(result.status)) controller.cancelAi=true;
        return result;
    });
    return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
}
StepStatus System::npcBarbFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto registered=ports_.monsters.registerWanderTarget(id);
    if(!registered) return isDeferred(registered.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    const auto &params=unit.rule.ai.params;
    controller.nextDecision=tick.tick+1;
    controller.pursuing=false;
    const auto *room=area.definition.activation.room(unit.position);
    const auto target=acquireTarget(unit,tick,false,room && !room->checkLosDraw && !controller.acquiredTarget);
    controller.target=target?std::optional{target->unit.id}:std::nullopt;
    if(target) {
        controller.acquiredTarget=true;
        if(unitMeleeReach(area.definition.collision,unit.position,unit.rule.size,target->position,target->size,unit.rule.meleeRange)) {
            controller.nextDecision=tick.tick+uint64_t(std::max(1,params[0]));
            const auto attack=requestAction(controller,{id,0,target->unit,tick.tick,4});
            return isDeferred(attack.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        if(target->distance<params[2] && int(rollRandom(controller.random)%100)<params[1]) {
            bool run=true;
            const auto curse=unit.rule.formulas->skillIds.find("decrepify");
            if(curse!=unit.rule.formulas->skillIds.end()) {
                const int state=unit.rule.formulas->skills.at(curse->second).auraTargetState;
                run=state<0 || !ports_.effects.unitStates(id,tick.tick).contains(state);
            }
            const auto move=ports_.monsters.requestMove({id,{unit.area,area.generation,target->position},target->unit.id,0,run?175:75,run,1});
            controller.pursuing=bool(move);
            return isDeferred(move.status)?StepStatus::Blocked:StepStatus::Complete;
        }
    }
    const Vec origin{std::floor(unit.position.x)+.5f,std::floor(unit.position.y)+.5f};
    const auto draw=[](uint64_t &random){return int(rollRandom(random)%20);};
    const auto walk=[&](int xShift,int yShift) {
        // D2MOO's argument evaluation order is unspecified; retail Fn127
        // confirms X first, then Y, and no retry draws until a failed path.
        const int x=draw(controller.random)+xShift;
        const int y=draw(controller.random)+yShift;
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,origin+Vec{float(x),float(y)}},{},0,75,false,1});
        controller.pursuing=bool(moved);return moved;
    };
    auto moved=walk(-40,-10);
    if(moved || isDeferred(moved.status)) return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
    const bool flip=(rollRandom(controller.random)&1)!=0;
    moved=walk(-10,-30*(2*int(flip)-1)-10);
    if(moved || isDeferred(moved.status)) return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
    moved=walk(-10,-30*(2*int(!flip)-1)-10);
    if(moved || isDeferred(moved.status)) return isDeferred(moved.status)?StepStatus::Blocked:StepStatus::Complete;
    ports_.monsters.neutralMode(id);controller.nextDecision=tick.tick+15;
    return StepStatus::Complete;
}
}
