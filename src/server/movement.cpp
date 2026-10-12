#include "movement.hpp"
#include "player_store.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/items/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "runtime/contracts.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server {
namespace {
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
    p.route.clear();p.position=next;
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
    p.route.clear();p.position=next;p.look=delta.unit();p.moving=true;p.runningNow=true;
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
        if(!at || at->region!=actor.area || nativeUnitDistance(player.position,2,at->position,1)>50) return CommandStatus::InvalidRequest;
        return applyMovement(player,area,{MovementAction::Move,at->position,command.forceRun,{},MotionTarget{command.exit,4},
            InteractionTarget{command.exit,at->position,at->position,1,1,4,false,true}});
    }
    if(command.action==MovementAction::ApproachNpc) if(const auto *target=ports.monsters.find(command.exit);target && target->area==actor.area && target->life>0) {
        if((target->position-player.position).length()>50) return CommandStatus::InvalidRequest;
        return applyMovement(player,area,{MovementAction::Move,target->position,command.forceRun,{},MotionTarget{target->id,1}});
    }
    if(command.action==MovementAction::ApproachCorpse) if(const auto *target=ports.players.findActor(command.exit);target && target->entered && target->area==actor.area && target->actor!=actor.actor) {
        if((target->position-player.position).length()>50) return CommandStatus::InvalidRequest;
        return applyMovement(player,area,{MovementAction::Move,target->position,command.forceRun,{},MotionTarget{target->actor,0}});
    }
    if(command.action==MovementAction::ApproachObject) if(const auto position=ports.travel.portalPosition(actor,command.exit)) {
        if((*position-player.position).length()>50) return CommandStatus::InvalidRequest;
        return applyMovement(player,area,{MovementAction::Move,*position,command.forceRun,{},MotionTarget{command.exit,2}});
    }
    return applyMovement(player, area, command);
}
void MovementSystem::step(TickContext tick) {
    const auto &ports = ports_;
    for (auto &[id, player] : ports.players.players_) {
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
                    dynamic=true;if(unit->area==player.area && unit->life>0) position=unit->position;
                }
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
            else if(position && (std::floor(position->x)!=std::floor(player.routeDestination.x) || std::floor(position->y)!=std::floor(player.routeDestination.y))) {
                auto arrival=player.movementArrival;if(arrival) arrival->pos=arrival->accessPoint=*position;
                applyMovement(player,ports.areas.at(player.area),{MovementAction::Move,*position,player.routeRunning,{},target,arrival});
            }
        }
        const auto from=player.position;
        const auto &area=ports.areas.at(player.area);
        if(!player.route.empty() && player.routeObstacles!=area.definition.collision.obstacleRevision)
            applyMovement(player,area,{MovementAction::Move,player.routeDestination,player.routeRunning,{},player.movementTarget,player.movementArrival});
        advanceMovement(player, area, TickContext::seconds);
        if(!barrierSegment(ports.monsters,player.area,from,player.position,2)) {player.position=from;player.route.clear();player.moving=false;}
    }
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
        if (found == exits.end() || std::abs(found->position.x - player.position.x) > 50 ||
            std::abs(found->position.y - player.position.y) > 50) return CommandStatus::InvalidRequest;
        target = found->arrival;
    }
    if (command.action == MovementAction::ApproachCorpse) {
        const auto &corpses = player.persistent.corpses;
        const auto corpse = std::find_if(corpses.begin(), corpses.end(), [&](const auto &value) { return value.id == command.exit && value.region == player.area && value.owner == player.actor; });
        if (corpse == corpses.end() || (corpse->position - player.position).length() > 50) return CommandStatus::InvalidRequest;
        target = corpse->position;
    }
    if (command.action == MovementAction::ApproachNpc) {
        const auto &npcs=area.definition.npcs; const auto found=std::find_if(npcs.begin(),npcs.end(),[&](const auto &npc){return npc.id==command.exit;});
        if (found==npcs.end() || (found->position-player.position).length()>50) return CommandStatus::InvalidRequest;
        arrival=InteractionTarget{found->id,found->position,found->position,found->rule.size,found->rule.size,6.f,false,true,false};
        target=found->position;
    }
    if (command.action == MovementAction::ApproachObject) {
        const auto &objects = area.definition.objects;
        const auto found = std::find_if(objects.begin(), objects.end(), [&](const auto &object) { return object.id == command.exit; });
        if (found == objects.end() || (found->position - player.position).length() > 50) return CommandStatus::InvalidRequest;
        const auto &rule = found->rule;
        arrival=InteractionTarget{found->id, found->position, found->position, rule.width, rule.height, float(rule.range), true};
        target=found->position;
    }
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || target.x < 0 || target.y < 0 ||
        target.x >= grid.width || target.y >= grid.height) return CommandStatus::InvalidDestination;
    if(arrival && interactionClear(grid,player.position,*arrival))
        return applyMovement(player,area,{MovementAction::Stop,{},false});
    const bool sameTarget=(!command.target && !player.movementTarget) ||
        (command.target && player.movementTarget && command.target->id==player.movementTarget->id && command.target->type==player.movementTarget->type);
    if(!player.route.empty() && sameTarget && player.routeObstacles==grid.obstacleRevision &&
        std::floor(target.x)==std::floor(player.routeDestination.x) && std::floor(target.y)==std::floor(player.routeDestination.y)) {
        player.routeRunning=command.forceRun;player.movementArrival=arrival;
        return CommandStatus::Applied;
    }
    player.routeDestination=target;player.routeObstacles=grid.obstacleRevision;
    // D2Common Straight is shared with prediction. Convert only at the grid
    // boundary; native packet coordinates name subtiles, not cell centers.
    constexpr Vec center{.5f,.5f};
    const uint8_t targetType=command.target?command.target->type:command.action==MovementAction::ApproachObject?2:
        command.action==MovementAction::ApproachNpc?1:command.action==MovementAction::ApproachCorpse?0:255;
    auto route = grid.nativePlayerPath(player.position+center, target+center, playerMovement,targetType);
    for(auto &point:route) point=point-center;
    if(arrival && (route.empty() || !interactionClear(grid,route.back(),*arrival))) {
        if(const auto approach=interactionApproach(grid,player.position,*arrival)) {
            route=grid.nativePlayerPath(player.position+center,*approach+center,playerMovement);
            for(auto &point:route) point=point-center;
        }
    }
    if (route.empty()) {
        player.route.clear(); player.moving = false;
        player.movementTarget.reset();player.movementArrival.reset();
        return CommandStatus::NoRoute;
    }
    player.route = std::move(route);
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
    while (remaining > 0 && !player.route.empty()) {
        if(player.movementArrival && interactionClear(area.definition.collision,player.position,*player.movementArrival)) {
            player.route.clear();break;
        }
        const Vec delta = player.route.front() - player.position;
        const float distance = delta.length();
        if (distance < .0001f) { player.route.pop_front(); continue; }
        const float step = std::min(remaining, distance);
        const Vec next = player.position + delta * (step / distance);
        // Step.cpp checks the travelled cells; re-running PATH_RayTrace on a
        // fractional frame changes its major-axis bias and can reject a valid leg.
        constexpr Vec center{.5f,.5f};
        if (!area.definition.collision.segment(player.position+center, next+center, {}, playerMovement)) {
            player.route.clear(); break;
        }
        player.look = delta.unit(); player.position = next; player.moving = true;
        remaining -= step;
        if (step >= distance) player.route.pop_front();
    }
}
} // namespace d2x::server
