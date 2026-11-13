#include "movement.hpp"
#include "server/runtime/skill_rules.hpp"
#include "area_store.hpp"
#include "player_store.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/skills/shape.hpp"
#include "gameplay/actions/movement_timing.hpp"
#include "gameplay/actions/shape_timing.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/pd2_phasing.hpp"
#include "gameplay/combat/geometry.hpp"
#include "runtime/contracts.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/facts/travel.hpp"
#include "world/interaction_geometry.hpp"
#include "world/native_target_path.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace d2x::server {
namespace {
MovementCollisionRule movementCollision(const PlayerState &player) {
    if(player.rules.skills && player.rules.skills->pd2 &&
       std::any_of(player.transient.states.begin(),player.transient.states.end(),pd2PhasingState))
        return {pd2PhasingMovementMask(false),playerMovement.size};
    return playerMovement;
}
void advanceMovement(PlayerState &,float,const Grid &,const ShapeProfile *);
bool nativeUnitRequestRange(Vec player, Vec target) {
    // PlrMsg 02/04 use sub_6FC83C60: native integer X/Y deltas <=50.
    // D2Common_10399 is the separate interaction/attack distance metric.
    const int x = int(std::floor(player.x+.5f)), y = int(std::floor(player.y+.5f));
    return std::abs(x-int(std::floor(target.x+.5f))) <= 50 &&
        std::abs(y-int(std::floor(target.y+.5f))) <= 50;
}
std::optional<Vec> movementPoint(const AreaStore &areas, RegionId from, RegionId to, Vec point) {
    return areas.linkedPosition(from,to,point);
}
CommandStatus invalidCoordinateDestination(PlayerState &player,const AreaState &area,const Grid &grid,Vec target) {
    constexpr Vec center{.5f,.5f};
    player.routeDestination=target;
    player.position=player.pathStep.stop(player.position+center)-center;
    applyMovement(player,area,{MovementAction::Stop,{},false},grid);
    return CommandStatus::InvalidDestination;
}
std::optional<std::pair<Vec, int>> npcPoint(const AreaStore &areas,const monsters::System &monsters, RegionId region, EntityId id) {
    const auto *unit=monsters.find(id);if(!unit || !unit->questNpc || !unit->alive()) return {};
    for (const auto &[key, area] : areas.all()) for (const auto &npc : area.definition.npcs)
        if (npc.id == id && npc.rule.interactable)
            if (const auto point = movementPoint(areas, region, key, unit->position)) return std::pair{*point, npc.rule.size};
    return {};
}
}
CommandStatus MovementSystem::apply(PlayerState &player,const AreaState &area,const MovementCommand &command) {
    const auto target=command.target?command.target->id:command.exit;
    const auto grid=ports_.spatial.navigationGrid(player.area,player.actor,target);
    if(command.coordinateRequest) {
        const auto global=area.definition.origin+command.destination;
        if(!ports_.areas.roomAtPosition(player.area,player.position+Vec{.5f,.5f},
            int(std::floor(global.x+.5f)),int(std::floor(global.y+.5f))))
            return invalidCoordinateDestination(player,area,grid,command.destination);
    }
    auto prepared=command;
    if(prepared.target && !prepared.arrival && prepared.target->type<=1) {
        int size=2;
        if(prepared.target->type==1) {
            const auto *unit=ports_.monsters.find(prepared.target->id);
            if(!unit) return CommandStatus::InvalidRequest;
            size=unit->rule.size;
        }
        prepared.arrival=InteractionTarget{target,prepared.destination,prepared.destination,size,size,0,false,true,false};
    }
    if(prepared.target && prepared.target->type==2 && prepared.arrival) {
        const auto object=ports_.objects.read().objects.find(prepared.target->id);
        if(object!=ports_.objects.read().objects.end()) prepared.arrival->doorOrientation=object->second.rule.doorOrientation;
    }
    return applyMovement(player,area,prepared,grid);
}
DomainResult<bool> MovementSystem::leapStep(const ActorContext &actor,Vec target,float speed,std::optional<MovementCollisionRule> collision) {
    auto it=ports_.players.players_.find(actor.player);
    if(it==ports_.players.players_.end() || it->second.actor!=actor.actor || it->second.area!=actor.area || !it->second.entered || it->second.persistent.player.hp<=0)
        return {DomainStatus::InvalidActor,{}};
    auto &p=it->second;const auto &area=ports_.areas.at(actor.area);
    if(area.generation!=actor.areaGeneration || !std::isfinite(speed) || speed<=0) return {DomainStatus::Stale,{}};
    const Vec delta=target-p.position;
    AreaStore::CollisionMove movement{ports_.areas,p.actor,p.area};
    const auto grid=ports_.spatial.movementGrid(p.area,movement);
    const Vec center{.5f,.5f};
    const auto step=p.pathStep.advance(grid,p.position+center,target+center,speed*TickContext::seconds,
        collision.value_or(MovementCollisionRule{0,2}));
    p.route.clear();p.position=step.position-center;movement.commit();
    ports_.areas.moveRoomUnit(p.actor,p.area,p.position);
    p.look=delta.unit();p.moving=step.travelled>0;p.runningNow=false;skillSteps_[actor.player]=actor.tick;
    if(step.blocked) {p.moving=false;return {DomainStatus::Unavailable,{}};}
    return {DomainStatus::Applied,step.reached};
}
DomainResult<bool> MovementSystem::chargeStep(const ActorContext &actor,Vec target,float speed,int stopDistance) {
    auto it=ports_.players.players_.find(actor.player);
    if(it==ports_.players.players_.end() || it->second.actor!=actor.actor || it->second.area!=actor.area || !it->second.entered || it->second.persistent.player.hp<=0)
        return {DomainStatus::InvalidActor,{}};
    auto &p=it->second;const auto &area=ports_.areas.at(actor.area);
    if(area.generation!=actor.areaGeneration || !std::isfinite(speed) || speed<=0) return {DomainStatus::Stale,{}};
    const Vec delta=target-p.position;const float distance=delta.length();
    if(distance<=float(stopDistance)) {p.moving=false;return {DomainStatus::Applied,true};}
    const Vec destination=stopDistance?target-delta.unit()*float(stopDistance):target;
    AreaStore::CollisionMove movement{ports_.areas,p.actor,p.area};
    const auto grid=ports_.spatial.movementGrid(p.area,movement);
    const Vec center{.5f,.5f};
    const auto step=p.pathStep.advance(grid,p.position+center,destination+center,speed*TickContext::seconds,movementCollision(p));
    p.route.clear();p.position=step.position-center;movement.commit();
    ports_.areas.moveRoomUnit(p.actor,p.area,p.position);p.look=delta.unit();p.moving=step.travelled>0;p.runningNow=true;
    skillSteps_[actor.player]=actor.tick;
    if(step.blocked) {p.moving=false;return {DomainStatus::Unavailable,{}};}
    return {DomainStatus::Applied,step.reached};
}
CommandStatus MovementSystem::validateCoordinateRequest(const ActorContext &actor,const MovementCommand &command) {
    if(!command.coordinateRequest) return CommandStatus::Applied;
    const auto *player=ports_.players.find(actor.player);
    if(!player || player->actor!=actor.actor || player->area!=actor.area) return CommandStatus::InvalidBinding;
    if(!std::isfinite(command.destination.x) || !std::isfinite(command.destination.y)) return CommandStatus::InvalidRequest;
    if(nativeUnitRequestRange(player->position,command.destination)) {
        coordinateFrames_[actor.player]=actor.tick;
        return CommandStatus::Applied;
    }
    const auto previous=coordinateFrames_.find(actor.player);
    const auto last=previous==coordinateFrames_.end()?uint64_t{}:previous->second;
    if(actor.tick>last && actor.tick-last>25) {
        const auto result=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},
            {RepositionFact{actor.actor,actor.area,player->position,true,1}}});
        if(!result) return CommandStatus::QueueFull;
    }
    return CommandStatus::InvalidRequest;
}
CommandStatus MovementSystem::execute(const ActorContext &actor, const MovementCommand &command) {
    const auto &ports = ports_;
    const auto found = ports.players.players_.find(actor.player);
    if (found == ports.players.players_.end()) return CommandStatus::InvalidBinding;
    auto &player = found->second;
    const auto &area = ports.areas.at(player.area);
    if (!player.entered || player.persistent.player.hp <= 0) return CommandStatus::Unavailable;
    if (player.actor != actor.actor || player.area != actor.area || area.generation != actor.areaGeneration)
        return CommandStatus::Stale;
    if(command.coordinateRequest && (!std::isfinite(command.destination.x) || !std::isfinite(command.destination.y) ||
        !nativeUnitRequestRange(player.position,command.destination)))
        return CommandStatus::InvalidRequest;
    if(command.action==MovementAction::StartRunning || command.action==MovementAction::StopRunning) {
        // Native 53/54 affect an existing walk/run, not the player's toggle.
        if(player.moving && !player.route.empty() && !ports.effects.recovering(player.actor,actor.tick)) {
            const bool run=command.action==MovementAction::StartRunning;
            if(run?(!player.runningNow && !area.definition.town && player.persistent.player.stamina.raw()>0):player.runningNow) {
                player.routeRunning=run;player.runningNow=run;++player.movementSequence;
            }
        }
        return CommandStatus::Applied;
    }
    if(ports.effects.recovering(player.actor,actor.tick) && command.action!=MovementAction::Stop && command.action!=MovementAction::ToggleRunning)
        return CommandStatus::Unavailable;
    skillSteps_.erase(actor.player);
    if(command.action==MovementAction::ApproachItem) {
        const auto found=ports.items.read().world.items.find(command.exit);
        if(found==ports.items.read().world.items.end()) return CommandStatus::InvalidRequest;
        const auto *at=std::get_if<GroundLocation>(&found->second.location);
        if(!at || at->region!=actor.area || !nativeUnitRequestRange(player.position,at->position)) return CommandStatus::InvalidRequest;
        return apply(player,area,{MovementAction::Move,at->position,command.forceRun,{},MotionTarget{command.exit,4},
            InteractionTarget{command.exit,at->position,at->position,1,1,4,false,true}});
    }
    if(command.action==MovementAction::ApproachNpc) {
        if(const auto *target=ports.monsters.find(command.exit);target && target->alive()) {
            const auto point = movementPoint(ports.areas, actor.area, target->area, target->position);
            if (!point || !nativeUnitRequestRange(player.position,*point)) return CommandStatus::InvalidRequest;
            return apply(player,area,{MovementAction::Move,*point,command.forceRun,{},MotionTarget{target->id,1},
                InteractionTarget{target->id,*point,*point,target->rule.size,target->rule.size,0.f,false,true,false}});
        }
        if (const auto npc = npcPoint(ports.areas,ports.monsters, actor.area, command.exit)) {
            if (!nativeUnitRequestRange(player.position,npc->first)) return CommandStatus::InvalidRequest;
            return apply(player,area,{MovementAction::Move,npc->first,command.forceRun,{},MotionTarget{command.exit,1},
                InteractionTarget{command.exit,npc->first,npc->first,npc->second,npc->second,0.f,false,true,false}});
        }
    }
    if(command.action==MovementAction::ApproachCorpse) if(const auto *target=ports.players.findActor(command.exit);target && target->entered && target->area==actor.area && target->actor!=actor.actor) {
        if(!nativeUnitRequestRange(player.position,target->position)) return CommandStatus::InvalidRequest;
        return apply(player,area,{MovementAction::Move,target->position,command.forceRun,{},MotionTarget{target->actor,0},
            InteractionTarget{target->actor,target->position,target->position,2,2,0.f,false,true,false}});
    }
    if(command.action==MovementAction::ApproachCorpse) {
        // Approach has no inventory authority; death::recover separately checks
        // the owner's directed permission when interacting with the body.
        for (const auto &[id, owner] : ports.players.all()) for (const auto &corpse : owner.persistent.corpses) {
            if (corpse.id != command.exit || corpse.region != actor.area) continue;
            if (!nativeUnitRequestRange(player.position,corpse.position)) return CommandStatus::InvalidRequest;
            return apply(player,area,{MovementAction::Move,corpse.position,command.forceRun,{},MotionTarget{corpse.id,0},
                InteractionTarget{corpse.id,corpse.position,corpse.position,2,2,0.f,false,true,false}});
        }
    }
    if(command.action==MovementAction::ApproachObject) {
        if(const auto position=ports.travel.portalPosition(actor,command.exit)) {
            if(!nativeUnitRequestRange(player.position,*position)) return CommandStatus::InvalidRequest;
            return apply(player,area,{MovementAction::Move,*position,command.forceRun,{},MotionTarget{command.exit,2}});
        }
        // PlrMsg 02/04 resolves the live UNIT_OBJECT GUID, including random
        // objects and restored identities; AreaDefinition only describes presets.
        const auto object=ports.objects.read().objects.find(command.exit);
        if(object!=ports.objects.read().objects.end()) {
            const auto &target=object->second;
            const auto point=movementPoint(ports.areas,actor.area,target.area,target.position);
            if(!point || !nativeUnitRequestRange(player.position,*point)) return CommandStatus::InvalidRequest;
            return apply(player,area,{MovementAction::Move,*point,command.forceRun,{},MotionTarget{target.id,2},
                InteractionTarget{target.id,*point,*point,target.rule.width,target.rule.height,float(target.rule.range),true}});
        }
        return CommandStatus::InvalidRequest;
    }
    return apply(player, area, command);
}
void MovementSystem::step(TickContext tick) {
    for(const auto &[id,player]:ports_.players.all()) {
        if(!player.entered) continue;
        if(admitted_.contains(player.actor)) {
            const auto timers=ports_.frameEvents.events({FrameEventUnit::Player,player.actor});
            if(std::any_of(timers.begin(),timers.end(),[](const auto &timer){return timer.type==FrameEventType::ModeChange && timer.continuous && timer.customId==int32_t(movementTimerHandler);})) continue;
        }
        auto timer=ports_.frameEvents.prepareInsertion({{},{FrameEventUnit::Player,player.actor},FrameEventType::ModeChange,
            tick.tick+1,true,0,int32_t(movementTimerHandler)});
        admitted_.insert(player.actor);ports_.frameEvents.publish(std::move(timer));
    }
    std::erase_if(admitted_,[&](EntityId id){const auto *p=ports_.players.findActor(id);return !p || !p->entered;});
    std::erase_if(staminaFrames_,[&](const auto &entry){const auto *player=ports_.players.find(entry.first);return !player || !player->entered;});
    std::erase_if(coordinateFrames_,[&](const auto &entry){return !ports_.players.find(entry.first);});
}
DomainResult<> MovementSystem::frameEvent(const FrameEvent &event,TickContext tick) {
    if(tick.tick<event.due) return {DomainStatus::Applied,std::monostate{}};
    const auto *source=ports_.players.findActor(event.owner.unit);
    if(!source || !source->entered) {ports_.frameEvents.cancel(event.handle);return {DomainStatus::Applied,std::monostate{}};}
    const auto only=source->player;
    const auto &ports = ports_;
    for (auto entry=ports.players.players_.find(only);entry!=ports.players.players_.end() && entry->first==only;++entry) {
        auto &[id,player]=*entry;
        (void)id;
        if(player.knockback) {
            const auto &area=ports.areas.at(player.area);
            const auto global=player.position+area.definition.origin;
            if(player.persistent.player.hp<=0 || !ports.effects.knockbackActive(player.actor) || player.teleportRevision!=player.knockback->teleportRevision ||
                global.x!=player.knockback->position.x || global.y!=player.knockback->position.y) {
                player.knockback.reset();
            } else {
                player.moving=false;player.runningNow=false;
                if(tick.tick<=player.knockback->started) continue;
                const auto previous=player.position;
                const auto previousStep=player.pathStep;
                const auto facing=player.position+area.definition.origin-player.knockback->destination;
                AreaStore::CollisionMove collisionMove{ports.areas,player.actor,player.area};
                const auto grid=ports.spatial.movementGrid(player.area,collisionMove);
                const Vec center{.5f,.5f};
                const auto step=player.pathStep.advance(grid,player.position+center,
                    player.knockback->destination-area.definition.origin+center,1.f,movementCollision(player));
                player.position=step.position-center;
                const ActorContext actor{player.player,player.actor,player.area,area.generation,player.locomotionSequence,tick.tick};
                const auto walked=ports.travel.walked(actor);
                if(!walked) {
                    player.position=previous;player.pathStep=previousStep;collisionMove.rollback();
                    return walked;
                }
                collisionMove.commit();
                if(facing.x!=0 || facing.y!=0) player.look=facing.unit();
                if(step.reached || step.blocked) {
                    player.position=player.pathStep.stop(player.position+center)-center;
                    player.knockback.reset();
                    ports.effects.finishKnockback(player.actor,tick.tick);
                } else player.knockback->position=player.position+ports.areas.at(player.area).definition.origin;
                ports.areas.moveRoomUnit(player.actor,player.area,player.position);
                continue;
            }
        }
        if(const auto step=skillSteps_.find(id);step!=skillSteps_.end() && step->second==tick.tick) {skillSteps_.erase(step);continue;}
        skillSteps_.erase(id);
        if(ports.effects.recovering(player.actor,tick.tick)) {player.moving=false;player.runningNow=false;continue;}
        // Arrival uses the live target, but the retail active path tolerates
        // five subtiles of target movement before rebuilding its points.
        if(player.movementTarget && !player.route.empty()) {
            const auto target=*player.movementTarget;
            std::optional<Vec> position;
            bool dynamic=false;
            if(target.type==1) {
                if(const auto *unit=ports.monsters.find(target.id)) {
                    dynamic=true;if(unit->alive()) position=movementPoint(ports.areas,player.area,unit->area,unit->position);
                }
                else if (const auto npc = npcPoint(ports.areas,ports.monsters, player.area, target.id)) { dynamic = true; position = npc->first; }
            } else if(target.type==0 && target.id!=player.actor) {
                dynamic=true;
                if(const auto *unit=ports.players.findActor(target.id);unit && unit->entered && unit->area==player.area) position=unit->position;
                if(!position) for(const auto &[ownerId,owner]:ports.players.all()) {
                    (void)ownerId;
                    for(const auto &corpse:owner.persistent.corpses)
                        if(corpse.id==target.id && corpse.region==player.area) position=corpse.position;
                }
            } else if(target.type==2) {
                dynamic=true;
                position=ports.travel.portalPosition({player.player,player.actor,player.area,
                    ports.areas.at(player.area).generation,player.locomotionSequence,tick.tick},target.id);
                if(!position) {
                    const auto object=ports.objects.read().objects.find(target.id);
                    if(object!=ports.objects.read().objects.end())
                        position=movementPoint(ports.areas,player.area,object->second.area,object->second.position);
                }
            } else if(target.type==4) {
                dynamic=true;
                const auto found=ports.items.read().world.items.find(target.id);
                if(found!=ports.items.read().world.items.end())
                    if(const auto *at=std::get_if<GroundLocation>(&found->second.location);at && at->region==player.area) position=at->position;
            }
            if(dynamic && !position) {player.route.clear();player.movementTarget.reset();player.movementArrival.reset();}
            else if(position) {
                if(player.movementArrival) player.movementArrival->pos=player.movementArrival->accessPoint=*position;
                if(nativeUnitTargetMoved(player.routeDestination,*position,target.type))
                    apply(player,ports.areas.at(player.area),{MovementAction::Move,*position,player.routeRunning,{},target,player.movementArrival});
            }
        }
        if(!player.route.empty() && player.pathStep.changedCell()) {
            const auto result=ports.effects.movementSkills(player.actor,tick.tick,player.pathStep.direction(),player.pathStep.nativePosition());
            if(!result) return result;
        }
        const auto from=player.position;
        const auto &area=ports.areas.at(player.area);
        if(!player.route.empty() && player.routeRunning) {
            const auto previous=staminaFrames_.find(id);
            if(previous==staminaFrames_.end() || previous->second!=tick.tick) {
                const auto drained=ports.effects.drainRunningStamina(player.actor,tick.tick);
                if(!drained) return drained;
                staminaFrames_[id]=tick.tick;
            }
            if(player.persistent.player.stamina.raw()<=0) {
                player.routeRunning=false;player.runningNow=false;
                apply(player,area,{MovementAction::Move,player.routeDestination,false,{},player.movementTarget,player.movementArrival});
            }
        }
        // D2Common_10226 keeps its remaining points while rooms are recached.
        // Unrelated room/object changes must not rebuild an in-flight route;
        // entered-cell collision is checked by the native step itself.
        const auto previousRoute = player.route;
        const auto previousStep = player.pathStep;
        AreaStore::CollisionMove collisionMove{ports.areas,player.actor,player.area};
        const auto movementGrid=ports.spatial.movementGrid(player.area,collisionMove);
        const auto *shape=player.rules.skills?skills::activeShape(*player.rules.skills,ports.effects.unitStates(player.actor,tick.tick)):nullptr;
        advanceMovement(player, TickContext::seconds,movementGrid,shape);
        const ActorContext actor{player.player, player.actor, player.area, area.generation, player.locomotionSequence, tick.tick};
        if (!ports.travel.walked(actor)) {
            player.position = from; player.route = previousRoute; player.pathStep = previousStep; player.moving = false;
            collisionMove.rollback();
        } else collisionMove.commit();
        ports.areas.moveRoomUnit(player.actor,player.area,player.position);
        if(player.moving) {
            const auto origin=ports.areas.at(player.area).definition.origin;
            const Vec point{std::floor(player.position.x+.5f)+origin.x,std::floor(player.position.y+.5f)+origin.y};
            const Vec travelled=player.movementHistory.empty()?point:point-player.movementHistory.back();
            if(player.movementHistory.empty() || travelled.x*travelled.x+travelled.y*travelled.y>45) {
                player.movementHistory.push_back(point);
                if(player.movementHistory.size()>20) player.movementHistory.pop_front();
            }
        }
    }
    return {DomainStatus::Applied,std::monostate{}};
}
void MovementSystem::suspend() {
    skillSteps_.clear();
    staminaFrames_.clear();
    const auto &ports = ports_;
    for (auto &[id, player] : ports.players.players_) {
        (void)id;
        player.route.clear(); player.moving = false;
        player.knockback.reset();
        player.movementTarget.reset();player.movementArrival.reset();
    }
}
CommandStatus applyMovement(PlayerState &player, const AreaState &area, const MovementCommand &command,const Grid &grid) {
    switch (command.action) {
    case MovementAction::Stop:
        player.route.clear(); player.moving = false; player.runningNow=false;
        player.movementTarget.reset(); player.movementArrival.reset();
        return CommandStatus::Applied;
    case MovementAction::ToggleRunning:
        player.running = !player.running;
        player.routeRunning = player.running;
        return CommandStatus::Applied;
    case MovementAction::Move:
    case MovementAction::ApproachExit:
    case MovementAction::ApproachCorpse:
    case MovementAction::ApproachNpc: break;
    default: return CommandStatus::Stale;
    }
    Vec target = command.destination;
    auto arrival=command.arrival;
    if (command.action == MovementAction::ApproachExit) {
        const auto &exits = area.definition.exits;
        const auto found = std::find_if(exits.begin(), exits.end(), [&](const auto &exit) { return exit.id == command.exit; });
        if (found == exits.end() || !nativeUnitRequestRange(player.position,found->position)) return CommandStatus::InvalidRequest;
        target = found->position;
        arrival=InteractionTarget{found->id,target,target,1,1,0,false,true,false};
    }
    if (command.action == MovementAction::ApproachCorpse) {
        const auto &corpses = player.persistent.corpses;
        const auto corpse = std::find_if(corpses.begin(), corpses.end(), [&](const auto &value) { return value.id == command.exit && value.region == player.area && value.owner == player.actor; });
        if (corpse == corpses.end() || !nativeUnitRequestRange(player.position,corpse->position)) return CommandStatus::InvalidRequest;
        target = corpse->position;
    }
    constexpr Vec center{.5f,.5f};
    if (!grid.navigationContains(target + center))
        return command.coordinateRequest?invalidCoordinateDestination(player,area,grid,target):CommandStatus::InvalidDestination;
    if(arrival && nativeUnitDistance(player.position,2,arrival->pos,arrival->collisionWidth)==0) {
        player.position=player.pathStep.stop(player.position+center)-center;
        return applyMovement(player,area,{MovementAction::Stop,{},false},grid);
    }
    // PlrModes::StartXY/StartID -> sub_6FC7F600 runs D2Common_10142
    // for each admitted command, including another click at the same point.
    // Passive pursuit has a separate native target-displacement threshold.
    player.routeDestination=target;player.routeObstacles=grid.navigationRevision();
    // D2Common Straight is shared with prediction. Convert only at the grid
    // boundary; native packet coordinates name subtiles, not cell centers.
    const uint8_t targetType=command.target?command.target->type:command.action==MovementAction::ApproachExit?5:
        command.action==MovementAction::ApproachNpc?1:command.action==MovementAction::ApproachCorpse?0:255;
    auto route = grid.nativePlayerPath(player.position+center, target+center, movementCollision(player),targetType,
        arrival?arrival->doorOrientation:std::nullopt);
    for(auto &point:route) point=point-center;
    if (route.empty()) {
        player.position=player.pathStep.stop(player.position+center)-center;
        player.route.clear(); player.moving = false;player.runningNow=false;
        player.movementTarget.reset();player.movementArrival.reset();
        return CommandStatus::NoRoute;
    }
    player.route = std::move(route);
    // Replacing path points does not replace native precise coordinates.
    player.movementTarget=command.target;
    player.movementArrival=arrival;
    if(command.exit) {
        if(command.action==MovementAction::ApproachExit) player.movementTarget=MotionTarget{command.exit,5};
        else if(command.action==MovementAction::ApproachCorpse) player.movementTarget=MotionTarget{command.exit,0};
        else if(command.action==MovementAction::ApproachNpc) player.movementTarget=MotionTarget{command.exit,1};
    }
    player.routeRunning = command.forceRun && player.persistent.player.stamina.raw()>0;
    player.runningNow = player.routeRunning;
    return CommandStatus::Applied;
}
namespace {
void advanceMovement(PlayerState &player, float seconds,const Grid &grid,const ShapeProfile *shape) {
    player.moving = false;
    if (!player.entered || player.persistent.player.hp <= 0) { player.route.clear(); return; }
    if(player.route.empty()) {player.runningNow=false;return;}
    player.runningNow = player.routeRunning;
    float remaining = (player.runningNow ? player.totals.character.runSpeed : player.totals.character.walkSpeed) * seconds;
    if(shape) {
        const auto mode=shapeMonsterMode(player.runningNow?3:2,shape->enabledModes);
        remaining=0;
        if(mode==2 || mode==15) {
            const auto faster=logicalStatValue(player.totals.nativeStats,*player.rules.character->stats,"item_fastermovevelocity");
            const auto timing=playerMovementTiming(player.definition.walkVelocity,player.definition.runVelocity,
                faster,player.totals.character.velocityPercent,mode==15);
            remaining=float(nativeMovementTiming(shape->walkVelocity,0,timing.percentage).velocity)*25.f/4096.f*seconds;
        }
    }
    while (remaining > 0 && !player.route.empty()) {
        if(player.movementArrival && nativeUnitDistance(player.position,2,player.movementArrival->pos,player.movementArrival->collisionWidth)==0) {
            player.route.clear();break;
        }
        const Vec delta = player.route.front() - player.position;
        constexpr Vec center{.5f,.5f};
        const auto step = player.pathStep.advance(grid, player.position + center,
            player.route.front() + center, remaining,movementCollision(player));
        player.look = delta.unit();
        player.moving = player.moving || step.travelled > 0;
        player.position = step.position - center;
        if (step.blocked) { player.route.clear(); break; }
        if (!step.reached) break;
        remaining -= step.travelled;
        player.route.pop_front();
        if (step.travelled > 0) break; // D2Common_10226 advances one precise path point per game frame.
    }
    if(player.route.empty()) {
        constexpr Vec center{.5f,.5f};
        player.position=player.pathStep.stop(player.position+center)-center;
        player.moving=false;player.runningNow=false;
    }
}
} // namespace
} // namespace d2x::server
