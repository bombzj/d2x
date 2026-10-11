#include "movement.hpp"
#include "player_store.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
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
        const auto from=player.position;
        advanceMovement(player, ports.areas.at(player.area), TickContext::seconds);
        if(!barrierSegment(ports.monsters,player.area,from,player.position,2)) {player.position=from;player.route.clear();player.moving=false;}
    }
}
void MovementSystem::suspend() {
    skillSteps_.clear();
    const auto &ports = ports_;
    for (auto &[id, player] : ports.players.players_) {
        (void)id;
        player.route.clear(); player.moving = false;
    }
}
CommandStatus applyMovement(PlayerState &player, const AreaState &area, const MovementCommand &command) {
    switch (command.action) {
    case MovementAction::Stop:
        player.route.clear(); player.moving = false;
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
        const InteractionTarget npc{found->id,found->position,found->position,found->rule.size,found->rule.size,3.f,true};
        if (interactionClear(grid,player.position,npc)) { player.route.clear(); player.moving=false; return CommandStatus::Applied; }
        const auto approach=interactionApproach(grid,player.position,npc); if (!approach) return CommandStatus::NoRoute; target=*approach;
    }
    if (command.action == MovementAction::ApproachObject) {
        const auto &objects = area.definition.objects;
        const auto found = std::find_if(objects.begin(), objects.end(), [&](const auto &object) { return object.id == command.exit; });
        if (found == objects.end() || (found->position - player.position).length() > 50) return CommandStatus::InvalidRequest;
        const auto &rule = found->rule;
        const InteractionTarget object{found->id, found->position, found->position, rule.width, rule.height, float(rule.range), true};
        if (interactionClear(grid, player.position, object)) { player.route.clear(); player.moving = false; return CommandStatus::Applied; }
        const auto approach = interactionApproach(grid, player.position, object); if (!approach) return CommandStatus::NoRoute;
        target = *approach;
    }
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || target.x < 0 || target.y < 0 ||
        target.x >= grid.width || target.y >= grid.height) return CommandStatus::InvalidDestination;
    // Existing area navigation may stop at the closest reachable point.
    // Only the authority chooses and consumes this route.
    auto route = grid.path(player.position, target, true, playerMovement);
    if (route.empty()) {
        player.route.clear(); player.moving = false;
        return CommandStatus::NoRoute;
    }
    player.route = std::move(route);
    player.movementTarget=command.target;
    if(command.exit) {
        if(command.action==MovementAction::ApproachExit) player.movementTarget=MotionTarget{command.exit,5};
        else if(command.action==MovementAction::ApproachCorpse) player.movementTarget=MotionTarget{command.exit,0};
        else if(command.action==MovementAction::ApproachObject) player.movementTarget=MotionTarget{command.exit,2};
        else if(command.action==MovementAction::ApproachNpc) player.movementTarget=MotionTarget{command.exit,1};
    }
    player.routeRunning = player.running || command.forceRun;
    return CommandStatus::Applied;
}
void advanceMovement(PlayerState &player, const AreaState &area, float seconds) {
    player.moving = false;
    if (!player.entered || player.persistent.player.hp <= 0) { player.route.clear(); return; }
    player.runningNow = player.routeRunning && (area.definition.town || player.persistent.player.stamina >= 1.f);
    float remaining = (player.runningNow ? player.totals.character.runSpeed : player.totals.character.walkSpeed) * seconds;
    while (remaining > 0 && !player.route.empty()) {
        const Vec delta = player.route.front() - player.position;
        const float distance = delta.length();
        if (distance < .0001f) { player.route.pop_front(); continue; }
        const float step = std::min(remaining, distance);
        const Vec next = player.position + delta * (step / distance);
        if (!area.definition.collision.nativeMovementSegment(player.position, next, playerMovement)) {
            player.route.clear(); break;
        }
        player.look = delta.unit(); player.position = next; player.moving = true;
        remaining -= step;
        if (step >= distance) player.route.pop_front();
    }
}
} // namespace d2x::server
