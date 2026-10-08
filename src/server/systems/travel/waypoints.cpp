#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
namespace d2x::server::travel {
DomainResult<> System::openWaypoint(const ActorContext &actor,EntityId id) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->area!=actor.area || p->actor!=actor.actor || p->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration || area->definition.waypointIndex<0) return {DomainStatus::InvalidActor,{}};
    const auto &objects=area->definition.objects; const auto object=std::find_if(objects.begin(),objects.end(),[&](const auto &o){return o.id==id && o.rule.operation==23;});
    if(object==objects.end()) return {DomainStatus::InvalidRequest,{}};
    const auto &r=object->rule; if(!interactionClear(area->definition.collision,p->position,{id,object->position,object->position,r.width,r.height,float(r.range),true})) return {DomainStatus::InvalidRequest,{}};
    auto access=state_.waypoints; access[actor.player]={id,actor.area,actor.areaGeneration};
    transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};
    edit.waypoints=p->persistent.waypoints; edit.waypoints->try_emplace(actor.area,float(actor.tick)/25.f);
    WaypointFact fact{id,{}}; for(const auto &[region,time]:*edit.waypoints) { (void)time; fact.unlocked.push_back(region); }
    edit.facts.emplace_back(std::move(fact)); auto plan=ports_.transactions.prepare(std::move(edit)); if(!plan) return {plan.status,{}};
    auto result=ports_.transactions.commit(std::move(*plan.value)); if(result) state_.waypoints.swap(access); return result;
}
DomainResult<> System::useSpecial(const ActorContext &actor,const Request &request) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    if(request.kind==Kind::Waypoint) {
        const auto access=state_.waypoints.find(actor.player);
        if(access==state_.waypoints.end() || access->second.source!=request.source.id || access->second.area!=actor.area || access->second.generation!=actor.areaGeneration) return {DomainStatus::InvalidRequest,{}};
        if(!request.destination || int(*request.destination)==0) { state_.waypoints.erase(access); return {DomainStatus::Applied,std::monostate{}}; }
        if(!p->persistent.waypoints.contains(*request.destination) || *request.destination==actor.area) return {DomainStatus::InvalidRequest,{}};
        const auto object=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[&](const auto &o){return o.id==request.source.id && o.rule.operation==23;});
        if(object==area->definition.objects.end()) return {DomainStatus::Stale,{}};
        const auto &r=object->rule; if(!interactionClear(area->definition.collision,p->position,{object->id,object->position,object->position,r.width,r.height,float(r.range),true})) return {DomainStatus::InvalidRequest,{}};
        auto prepared=ports_.world.requestWaypoint(actor,*request.destination); if(!prepared) return {prepared.status,{}};
        Transition next{actor.area,*request.destination,*prepared.value,actor.areaGeneration,actor.sequence,request.source.id,p->position,{},{},false,false,false,0,0}; next.kind=Kind::Waypoint;
        state_.transitions[actor.player]=next; auto &mutablePlayer=ports_.players.players_.at(actor.player); mutablePlayer.route.clear(); mutablePlayer.moving=false;
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(request.kind!=Kind::Portal) return {};
    for(const auto &[owner,portal]:state_.portals) {
        const bool returning=portal.town==actor.area && portal.townId==request.source.id;
        if(!returning && !(portal.field==actor.area && portal.fieldId==request.source.id)) continue;
        // Party membership authority has not been implemented; ownership is mandatory.
        if(owner!=actor.player || !portal.opened) return {DomainStatus::InvalidRequest,{}};
        const auto at=returning?portal.townPosition:portal.fieldPosition;
        if((at-p->position).length()>portal.rule.range || !area->definition.collision.segment(p->position,at,{},playerMovement)) return {DomainStatus::InvalidRequest,{}};
        Transition next{actor.area,returning?portal.field:portal.town,0,actor.areaGeneration,actor.sequence,request.source.id,p->position,returning?portal.fieldPosition:portal.townPosition,{},false,false,false,0,0}; next.kind=Kind::Portal;
        state_.transitions[actor.player]=next; auto &mutablePlayer=ports_.players.players_.at(actor.player); mutablePlayer.route.clear(); mutablePlayer.moving=false;
        return {DomainStatus::Applied,std::monostate{}};
    }
    return {DomainStatus::Stale,{}};
}
}
