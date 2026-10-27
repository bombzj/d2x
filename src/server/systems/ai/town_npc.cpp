#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/quests/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/units/distance.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

// D2MOO AiThink Fn032/Fn041/Fn054, ExecuteMapAiAction and sub_6FCE69A0.
// MIT: docs/licenses/D2MOO.txt. MapAI coordinates/actions come from DS1.
namespace d2x::server::ai {
StepStatus System::townNpcFamily(EntityId id,TickContext tick,Controller &controller) {
    {
        const auto *unit=ports_.monsters.find(id);
        if(!unit) return StepStatus::Complete;
        if(unit->rule.ai.kind!=MonsterAiKind::NpcStationary && unit->rule.ai.kind!=MonsterAiKind::NpcOutOfTown && !controller.homeCommand) {
            const bool fresh=!unit->aiHomeCommand;
            const auto home=ports_.monsters.ensureAiHomeCommand(id);
            if(!home) return isDeferred(home.status)?StepStatus::Blocked:StepStatus::NotImplemented;
            controller.homeCommand=*home.value;
            if(fresh) {
                const auto idle=ports_.monsters.aiPose(id,1,false,false,tick.tick);
                controller.nextDecision=tick.tick+20;
                return isDeferred(idle.status)?StepStatus::Blocked:StepStatus::Complete;
            }
        }
    }
    // Quest callbacks can replace an actor publication or retire the unit.
    // Reacquire it below; no borrowed actor survives this call.
    const auto story=ports_.quests.npcThink(id,tick.tick);
    if(!story) return isDeferred(story.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    if(story.value->handled) {
        controller.pursuing=story.value->walking;
        controller.nextDecision=tick.tick+uint64_t(story.value->waitFrames);
        if(story.value->home) controller.homeCommand=story.value->home;
        return StepStatus::Complete;
    }
    const auto &unit=*ports_.monsters.find(id);
    if(unit.rule.ai.kind==MonsterAiKind::NpcOutOfTown) return StepStatus::NotImplemented;
    const auto &area=ports_.areas.at(unit.area).definition;
    auto &command=controller.npc;
    const auto global=unit.position+area.origin;
    const auto distance=[&](Vec point) {
        return unitCoordinateDistance(int(std::floor(global.x)),int(std::floor(global.y)),
            int(std::floor(point.x)),int(std::floor(point.y)));
    };
    const auto idle=[&](int frames) {
        controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(frames);
        return perform(controller,[this,id,tick]{return ports_.monsters.aiPose(id,1,false,false,tick.tick);});
    };
    const auto walk=[&](Vec point,int steps=0,bool circle=false) {
        controller.nextDecision=tick.tick+1;
        const auto moved=perform(controller,[this,id,point,steps,circle] {
            const auto *actor=ports_.monsters.find(id);
            if(!actor || !actor->alive() || actor->rule.nativeVelocity<=0) return DomainResult<>{DomainStatus::Unavailable,{}};
            const auto &destination=ports_.areas.at(actor->area);
            // SetVelocity(7,0,0): native Straight path with base walk speed.
            // Command4 occasionally uses native clockwise circle path5.
            return ports_.monsters.requestMove({id,{actor->area,destination.generation,point-destination.definition.origin},
                {},0,75,false,steps,circle?1:0,circle?5:0,7,5});
        });
        controller.pursuing=bool(moved);return moved;
    };
    const auto finish=[](const DomainResult<> &result) {
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const bool stationary=unit.rule.ai.kind==MonsterAiKind::NpcStationary;
    bool interacting=false;
    for(const auto &[player,conversation]:ports_.npc.read().conversations)
        if(conversation.npc==id && ports_.npc.conversation(player)) {interacting=true;break;}
    if(interacting) return finish(idle(stationary?10:8));
    // A walk re-enters the native callback only when its path ends. Keep
    // commands and random draws untouched while the shared mode advances it.
    if(controller.pursuing && !unit.route.empty()) {controller.nextDecision=tick.tick+1;return StepStatus::Complete;}
    controller.pursuing=false;

    // AiTactics selector2 stops at the first qualifying player within
    // distance15. Use the active room order; do not globally sort by distance.
    const auto definition=std::find_if(area.npcs.begin(),area.npcs.end(),[&](const auto &npc){return npc.id==id;});
    const PlayerState *target=nullptr;Vec targetPoint;int targetDistance=INT32_MAX;
    if(unit.rule.ai.kind!=MonsterAiKind::Towner) if(const auto *room=area.activation.room(unit.position))
        for(const auto near:area.roomNear.at(size_t(room-area.activation.rooms().data()))) {
            for(const auto candidate:ports_.areas.roomUnitOrder(near)) {
                const auto *player=ports_.players.findActor(candidate);
                if(!player || !player->entered) continue;
                const auto point=player->position+ports_.areas.at(player->area).definition.origin;
                const int metric=unitCoordinateDistanceWithSize(int(std::floor(global.x)),int(std::floor(global.y)),
                    int(std::floor(point.x)),int(std::floor(point.y)),unit.rule.size);
                if(metric>15) continue;
                if(unit.rule.curseAiInteract) {
                    if(definition==area.npcs.end() || !ports_.quests.npcVisible(player->persistent.player,definition->rule.code,unit.area,definition->rule.questInitFunction)) continue;
                    const auto dialogue=ports_.quests.dialogue({player->player,player->actor,unit.area,ports_.areas.at(unit.area).generation,0,tick.tick},definition->rule);
                    if(!dialogue || dialogue->message.menu==2) continue;
                }
                target=player;targetPoint=point;targetDistance=metric;break;
            }
            if(target) break;
        }
    if(target) {
        ports_.quests.npcInteractionDecision(id,tick.tick);
        if(ports_.npc.conversation(target->player)) return finish(idle(stationary?10:8));
        if(stationary || targetDistance<=2 || targetDistance>=24) {
            if(!command.greetingCooldown) {
                command.greetingCooldown=60;
                const auto greeted=perform(controller,[this,id,player=target->player,region=unit.area,tick] {
                    const auto published=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Player,player,region},{SoundFact{id,1,region,18}}});
                    if(!published) return DomainResult<>{published.status,{}};
                    return DomainResult<>{DomainStatus::Applied,std::monostate{}};
                });
                if(isDeferred(greeted.status)) return StepStatus::Blocked;
            } else --command.greetingCooldown;
            return finish(idle(20));
        }
        if(distance(*controller.homeCommand)>16) {
            command.destination=*controller.homeCommand;command.moveCycles=12;command.waitFrames=10;
            return finish(idle(10));
        }
        return finish(walk(monsterRadiusApproachPoint(global,unit.rule.size,targetPoint,
            targetDistance<5?targetDistance-2:3,2),1));
    }
    if(stationary) return finish(idle(20));
    if(unit.rule.ai.kind==MonsterAiKind::JarJar) return finish(idle(20));

    // Native command4 precedes command7, including its repeated idle cycles.
    if(command.moveCycles>0) {
        --command.moveCycles;
        if(command.destination.x && command.destination.y && distance(command.destination)>3)
            return finish(walk(command.destination,0,(state_.npcMoveCounter++%4)==0));
        return finish(idle(command.waitFrames));
    }
    if(command.mode) {
        if(command.mode!=8 && command.mode!=9) {command.mode=0;return finish(idle(50));}
        const int remaining=distance(command.modePosition);
        if(remaining<=0 || command.modeAttempts<=0) {
            if(remaining>1) command.mode=0;
            if(unit.identity.monster=="larzuk" && limitedRandom(controller.random,100)>4) {command.mode=0;return finish(idle(50));}
            if(command.mode) {
                if(unit.nativeMode==command.mode) return finish(idle(50));
                const auto mode=command.mode;command.mode=0;
                std::optional<uint8_t> direction;
                // sub_6FCE69A0 fixes these work animations to native
                // directions 56,52,4 and 48 respectively (64-way path).
                if(unit.identity.monster=="charsi") direction=56;
                else if(unit.identity.monster=="warriv1") direction=52;
                else if(unit.identity.monster=="fara") direction=4;
                else if(unit.identity.monster=="jamella") direction=mode==8?52:48;
                const auto changed=perform(controller,[this,id,mode,direction,tick]{return ports_.monsters.aiPose(id,mode,false,false,tick.tick,0,false,direction);});
                controller.nextDecision=tick.tick+uint64_t(unit.rule.aiPoseTicks.at(mode))+uint64_t(unit.rule.neutralDelayTicks);
                return finish(changed);
            }
            if(unit.identity.monster=="fara" && limitedRandom(controller.random,100)<66)
                command.mode=unit.rule.aiPoseTicks.contains(8)?8:1;
        } else {
            --command.modeAttempts;
            const auto moved=walk(command.modePosition);
            if(!moved && !isDeferred(moved.status)) return finish(idle(25));
            return finish(moved);
        }
    }
    // Keep every authored node in the draw, including unsupported/no-op
    // actions 0 and 6. Filtering them changes both behaviour and unit seed.
    if(!unit.rule.mapAi.empty() && limitedRandom(controller.random,100)<66) {
        const auto &node=unit.rule.mapAi[limitedRandom(controller.random,uint32_t(unit.rule.mapAi.size()))];
        if(node.action>=1 && node.action<=5 && node.position.x && node.position.y) {
            const bool displaced=distance(node.position)!=0;
            if(displaced) {
                const auto moved=walk(node.position);
                if(isDeferred(moved.status)) return StepStatus::Blocked;
            }
            if(displaced || node.action==2 || node.action==4 || node.action==5) {
                command.destination=node.position;command.moveCycles=node.action==2?20:12;command.waitFrames=10;
                if(node.action==4 || node.action==5) {
                    const uint8_t mode=node.action==4?8:9;
                    command.mode=unit.rule.aiPoseTicks.contains(mode)?mode:1;
                    command.modePosition=node.position;command.modeAttempts=4;
                }
                if(displaced) return StepStatus::Complete;
            }
        }
    }
    return finish(idle(unit.rule.ai.kind==MonsterAiKind::Towner?12:8));
}
}
