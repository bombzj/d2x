#include "movement.hpp"
#include "player_store.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/items/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "runtime/contracts.hpp"
#include "world/interaction_geometry.hpp"
#include "world/native_target_path.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace d2x::server {
namespace {
bool nativeUnitRequestRange(Vec player, Vec target) {
    // PlrMsg 02/04 use sub_6FC83C60: native integer X/Y deltas <=50.
    // D2Common_10399 is the separate interaction/attack distance metric.
    const int x = int(std::floor(player.x+.5f)), y = int(std::floor(player.y+.5f));
    return std::abs(x-int(std::floor(target.x+.5f))) <= 50 &&
        std::abs(y-int(std::floor(target.y+.5f))) <= 50;
}
std::optional<Vec> movementPoint(const AreaStore &areas, RegionId from, RegionId to, Vec point) {
    const auto &source = areas.at(from);
    const auto *destination = areas.find(to);
    if (!destination || destination->definition.act != source.definition.act) return {};
    if (from != to) {
        Vec origin; const auto view = source.definition.collision.linkedNavigation(origin);
        if (std::none_of(view.navigationSources.begin(), view.navigationSources.end(),
            [&](const auto &entry) { return entry.grid == &destination->definition.collision; })) return {};
    }
    return point + destination->definition.origin - source.definition.origin;
}
std::optional<std::pair<Vec, int>> npcPoint(const AreaStore &areas,const monsters::System &monsters, RegionId region, EntityId id) {
    const auto *unit=monsters.find(id);if(!unit || !unit->questNpc || !unit->alive()) return {};
    for (const auto &[key, area] : areas.all()) for (const auto &npc : area.definition.npcs)
        if (npc.id == id && npc.rule.interactable)
            if (const auto point = movementPoint(areas, region, key, unit->position)) return std::pair{*point, npc.rule.size};
    return {};
}
bool barrierSegment(const monsters::System &monsters,RegionId area,Vec from,Vec to,int size) {
    for(const auto &[id,body]:monsters.read().actors) {(void)id;if(body.area==area && body.life>0 && body.implementation==MonsterKind::BoneWall && missileUnitIntersection(from,to,size,body.position,body.rule.size)) return false;}
    return true;
}
}
DomainResult<bool> MovementSystem::leapStep(const ActorContext &actor,Vec target,float speed,std::optional<MovementCollisionRule> collision) {
    auto it=ports_.players.players_.find(actor.player);
    if(it==ports_.players.players_.end() || it->second.actor!=actor.actor || it->second.area!=actor.area || !it->second.entered || it->second.persistent.player.hp<=0)
        return {DomainStatus::InvalidActor,{}};
    auto &p=it->second;const auto &area=ports_.areas.at(actor.area);
    if(area.generation!=actor.areaGeneration || !std::isfinite(speed) || speed<=0) return {DomainStatus::Stale,{}};
    const Vec delta=target-p.position;
    const auto next=p.position+delta.unit()*std::min(delta.length(),speed*TickContext::seconds);
    if(collision && !area.definition.collision.nativeMovementSegment(p.position,next,*collision)) {p.moving=false;return {DomainStatus::Unavailable,{}};}
    p.route.clear();p.position=next;ports_.areas.moveRoomUnit(p.actor,p.area,p.position);
    p.look=delta.unit();p.moving=true;p.runningNow=false;skillSteps_[actor.player]=actor.tick;
    return {DomainStatus::Applied,(target-p.position).length()<.001f};
}
DomainResult<bool> MovementSystem::chargeStep(const ActorContext &actor,Vec target,float speed,int stopDistance) {
    auto it=ports_.players.players_.find(actor.player);
    if(it==ports_.players.players_.end() || it->second.actor!=actor.actor || it->second.area!=actor.area || !it->second.entered || it->second.persistent.player.hp<=0)
        return {DomainStatus::InvalidActor,{}};
    auto &p=it->second;const auto &area=ports_.areas.at(actor.area);
    if(area.generation!=actor.areaGeneration || !std::isfinite(speed) || speed<=0) return {DomainStatus::Stale,{}};
    const Vec delta=target-p.position;const float distance=delta.length();
    if(distance<=float(stopDistance)) {p.moving=false;return {DomainStatus::Applied,true};}
    const Vec next=p.position+delta.unit()*std::min(distance-float(stopDistance),speed*TickContext::seconds);
    if(!area.definition.collision.nativeMovementSegment(p.position,next,playerMovement) || !barrierSegment(ports_.monsters,p.area,p.position,next,2)) {p.moving=false;return {DomainStatus::Unavailable,{}};}
    p.route.clear();p.position=next;ports_.areas.moveRoomUnit(p.actor,p.area,p.position);p.look=delta.unit();p.moving=true;p.runningNow=true;
    skillSteps_[actor.player]=actor.tick;
    return {DomainStatus::Applied,(target-next).length()<=float(stopDistance)+.001f};
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
    if(ports.effects.recovering(player.actor,actor.tick) && command.action!=MovementAction::Stop && command.action!=MovementAction::ToggleRunning)
        return CommandStatus::Unavailable;
    skillSteps_.erase(actor.player);
    if(command.action==MovementAction::ApproachItem) {
        const auto found=ports.items.read().world.items.find(command.exit);
        if(found==ports.items.read().world.items.end()) return CommandStatus::InvalidRequest;
        const auto *at=std::get_if<GroundLocation>(&found->second.location);
        if(!at || at->region!=actor.area || !nativeUnitRequestRange(player.position,at->position)) return CommandStatus::InvalidRequest;
        return applyMovement(player,area,{MovementAction::Move,at->position,command.forceRun,{},MotionTarget{command.exit,4},
            InteractionTarget{command.exit,at->position,at->position,1,1,4,false,true}});
    }
    if(command.action==MovementAction::ApproachNpc) {
        if(const auto *target=ports.monsters.find(command.exit);target && target->alive()) {
            const auto point = movementPoint(ports.areas, actor.area, target->area, target->position);
            if (!point || !nativeUnitRequestRange(player.position,*point)) return CommandStatus::InvalidRequest;
            return applyMovement(player,area,{MovementAction::Move,*point,command.forceRun,{},MotionTarget{target->id,1},
                InteractionTarget{target->id,*point,*point,target->rule.size,target->rule.size,0.f,false,true,false}});
        }
        if (const auto npc = npcPoint(ports.areas,ports.monsters, actor.area, command.exit)) {
            if (!nativeUnitRequestRange(player.position,npc->first)) return CommandStatus::InvalidRequest;
            return applyMovement(player,area,{MovementAction::Move,npc->first,command.forceRun,{},MotionTarget{command.exit,1},
                InteractionTarget{command.exit,npc->first,npc->first,npc->second,npc->second,0.f,false,true,false}});
        }
    }
    if(command.action==MovementAction::ApproachCorpse) if(const auto *target=ports.players.findActor(command.exit);target && target->entered && target->area==actor.area && target->actor!=actor.actor) {
        if(!nativeUnitRequestRange(player.position,target->position)) return CommandStatus::InvalidRequest;
        return applyMovement(player,area,{MovementAction::Move,target->position,command.forceRun,{},MotionTarget{target->actor,0},
            InteractionTarget{target->actor,target->position,target->position,2,2,0.f,false,true,false}});
    }
    if(command.action==MovementAction::ApproachObject) if(const auto position=ports.travel.portalPosition(actor,command.exit)) {
        if(!nativeUnitRequestRange(player.position,*position)) return CommandStatus::InvalidRequest;
        return applyMovement(player,area,{MovementAction::Move,*position,command.forceRun,{},MotionTarget{command.exit,2}});
    }
    return applyMovement(player, area, command);
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
        if(const auto step=skillSteps_.find(id);step!=skillSteps_.end() && step->second==tick.tick) {skillSteps_.erase(step);continue;}
        skillSteps_.erase(id);
        if(ports.effects.recovering(player.actor,tick.tick)) {player.moving=false;player.runningNow=false;continue;}
        // Unit paths follow the current target. Keep an unchanged route instead
        // of restarting Straight/AStar every simulation tick during pursuit.
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
            } else if(target.type==4) {
                dynamic=true;
                const auto found=ports.items.read().world.items.find(target.id);
                if(found!=ports.items.read().world.items.end())
                    if(const auto *at=std::get_if<GroundLocation>(&found->second.location);at && at->region==player.area) position=at->position;
            }
            if(dynamic && !position) {player.route.clear();player.movementTarget.reset();player.movementArrival.reset();}
            else if(position && (std::floor(position->x+.5f)!=std::floor(player.routeDestination.x+.5f) || std::floor(position->y+.5f)!=std::floor(player.routeDestination.y+.5f))) {
                auto arrival=player.movementArrival;if(arrival) arrival->pos=arrival->accessPoint=*position;
                applyMovement(player,ports.areas.at(player.area),{MovementAction::Move,*position,player.routeRunning,{},target,arrival});
            }
        }
        if(!player.route.empty() && player.pathStep.changedCell()) {
            const auto result=ports.effects.movementSkills(player.actor,tick.tick,player.pathStep.direction(),player.pathStep.nativePosition());
            if(!result) return result;
        }
        const auto from=player.position;
        const auto &area=ports.areas.at(player.area);
        // D2Common_10226 keeps its remaining points while rooms are recached.
        // Unrelated room/object changes must not rebuild an in-flight route;
        // entered-cell collision is checked by the native step itself.
        const auto previousRoute = player.route;
        const auto previousStep = player.pathStep;
        advanceMovement(player, area, TickContext::seconds);
        if(!barrierSegment(ports.monsters,player.area,from,player.position,2)) {player.position=from;player.route.clear();player.moving=false;}
        const ActorContext actor{player.player, player.actor, player.area, area.generation, player.locomotionSequence, tick.tick};
        if (!ports.travel.walked(actor)) {
            player.position = from; player.route = previousRoute; player.pathStep = previousStep; player.moving = false;
        }
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
    const auto &ports = ports_;
    for (auto &[id, player] : ports.players.players_) {
        (void)id;
        player.route.clear(); player.moving = false;
        player.movementTarget.reset();player.movementArrival.reset();
    }
}
CommandStatus applyMovement(PlayerState &player, const AreaState &area, const MovementCommand &command) {
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
    case MovementAction::ApproachObject:
    case MovementAction::ApproachNpc: break;
    default: return CommandStatus::Stale;
    }
    const auto &grid = area.definition.collision;
    Vec target = command.destination;
    auto arrival=command.arrival;
    if (command.action == MovementAction::ApproachExit) {
        const auto &exits = area.definition.exits;
        const auto found = std::find_if(exits.begin(), exits.end(), [&](const auto &exit) { return exit.id == command.exit; });
        if (found == exits.end() || !nativeUnitRequestRange(player.position,found->position)) return CommandStatus::InvalidRequest;
        target = found->arrival;
    }
    if (command.action == MovementAction::ApproachCorpse) {
        const auto &corpses = player.persistent.corpses;
        const auto corpse = std::find_if(corpses.begin(), corpses.end(), [&](const auto &value) { return value.id == command.exit && value.region == player.area && value.owner == player.actor; });
        if (corpse == corpses.end() || !nativeUnitRequestRange(player.position,corpse->position)) return CommandStatus::InvalidRequest;
        target = corpse->position;
    }
    if (command.action == MovementAction::ApproachObject) {
        const auto &objects = area.definition.objects;
        const auto found = std::find_if(objects.begin(), objects.end(), [&](const auto &object) { return object.id == command.exit; });
        if (found == objects.end() || !nativeUnitRequestRange(player.position,found->position)) return CommandStatus::InvalidRequest;
        const auto &rule = found->rule;
        arrival=InteractionTarget{found->id, found->position, found->position, rule.width, rule.height, float(rule.range), true};
        target=found->position;
    }
    constexpr Vec center{.5f,.5f};
    if (!grid.navigationContains(target + center)) return CommandStatus::InvalidDestination;
    if(arrival && interactionClear(grid,player.position,*arrival))
        return applyMovement(player,area,{MovementAction::Stop,{},false});
    const bool sameTarget=(!command.target && !player.movementTarget) ||
        (command.target && player.movementTarget && command.target->id==player.movementTarget->id && command.target->type==player.movementTarget->type);
    const auto obstacleRevision = grid.navigationRevision();
    if(!player.route.empty() && sameTarget && player.routeObstacles==obstacleRevision &&
        std::floor(target.x+.5f)==std::floor(player.routeDestination.x+.5f) && std::floor(target.y+.5f)==std::floor(player.routeDestination.y+.5f)) {
        player.routeRunning=command.forceRun;player.movementArrival=arrival;
        return CommandStatus::Applied;
    }
    player.routeDestination=target;player.routeObstacles=obstacleRevision;
    // D2Common Straight is shared with prediction. Convert only at the grid
    // boundary; native packet coordinates name subtiles, not cell centers.
    const uint8_t targetType=command.target?command.target->type:command.action==MovementAction::ApproachObject?2:
        command.action==MovementAction::ApproachNpc?1:command.action==MovementAction::ApproachCorpse?0:255;
    auto route = grid.nativePlayerPath(player.position+center, target+center, playerMovement,targetType);
    for(auto &point:route) point=point-center;
    if (route.empty()) {
        player.route.clear(); player.moving = false;
        player.movementTarget.reset();player.movementArrival.reset();
        return CommandStatus::NoRoute;
    }
    player.route = std::move(route);
    player.pathStep.reset();
    player.movementTarget=command.target;
    player.movementArrival=arrival;
    if(command.exit) {
        if(command.action==MovementAction::ApproachExit) player.movementTarget=MotionTarget{command.exit,5};
        else if(command.action==MovementAction::ApproachCorpse) player.movementTarget=MotionTarget{command.exit,0};
        else if(command.action==MovementAction::ApproachObject) player.movementTarget=MotionTarget{command.exit,2};
        else if(command.action==MovementAction::ApproachNpc) player.movementTarget=MotionTarget{command.exit,1};
    }
    player.routeRunning = command.forceRun;
    return CommandStatus::Applied;
}
void advanceMovement(PlayerState &player, const AreaState &area, float seconds) {
    player.moving = false;
    if (!player.entered || player.persistent.player.hp <= 0) { player.route.clear(); return; }
    player.runningNow = player.routeRunning && (area.definition.town || player.persistent.player.stamina >= 1.f);
    float remaining = (player.runningNow ? player.totals.character.runSpeed : player.totals.character.walkSpeed) * seconds;
    bool obstructed = false;
    while (remaining > 0 && !player.route.empty()) {
        if(player.movementArrival && interactionClear(area.definition.collision,player.position,*player.movementArrival)) {
            player.route.clear();break;
        }
        const Vec delta = player.route.front() - player.position;
        constexpr Vec center{.5f,.5f};
        const auto step = player.pathStep.advance(area.definition.collision, player.position + center,
            player.route.front() + center, remaining);
        player.look = delta.unit();
        player.moving = player.moving || step.travelled > 0;
        player.position = step.position - center;
        if (step.blocked) { player.route.clear(); obstructed = true; break; }
        if (!step.reached) break;
        remaining -= step.travelled;
        player.route.pop_front();
        if (step.travelled > 0) break; // D2Common_10226 advances one precise path point per game frame.
    }
    if (!obstructed && player.moving && player.route.empty() &&
        player.movementTarget && player.movementArrival &&
        (player.movementArrival->nativeObject || player.movementArrival->nativeUnit)) {
        player.route = nativeTargetPath(area.definition.collision, player.position, *player.movementArrival,
            player.movementTarget->type);
        player.pathStep.reset();
    }
}
} // namespace d2x::server
