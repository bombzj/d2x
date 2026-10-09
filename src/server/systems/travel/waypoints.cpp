#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/trade/system.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "gameplay/areas/waypoint.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
namespace d2x::server::travel {
bool System::waypointBusy(PlayerId id) const {
    if(ports_.npc.conversation(id) || ports_.inventory.storageAccess(id) || ports_.inventory.cubeAccess(id)) return true;
    for(const auto &[key,exchange]:ports_.trade.read().exchanges) {
        (void)key;if(exchange.first.player==id || exchange.second.player==id) return true;
    }
    return false;
}
const AreaObject *System::waypointAccess(const ActorContext &actor,EntityId id,std::optional<uint64_t> revision) const {
    const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 ||
        !area || area->generation!=actor.areaGeneration || !p->rules.character || waypointBusy(actor.player)) return nullptr;
    const auto access=state_.waypoints.find(actor.player);
    if(access==state_.waypoints.end() || access->second.source!=id || access->second.area!=actor.area ||
        access->second.generation!=actor.areaGeneration || (revision && access->second.revision!=*revision)) return nullptr;
    const auto object=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[&](const auto &o){return o.id==id && o.rule.operation==23;});
    if(object==area->definition.objects.end()) return nullptr;
    // PlrMsg::Rcv0x49 uses independent integer X/Y bounds, without the
    // object interaction ray. Sorceress has22, all other classes have10.
    const int range=p->definition.code=="sor"?22:10;
    if(std::abs(int(object->position.x)-int(p->position.x))>range ||
        std::abs(int(object->position.y)-int(p->position.y))>range) return nullptr;
    return &*object;
}
DomainResult<> System::openWaypoint(const ActorContext &actor,EntityId id,bool menu,std::optional<int> remoteRange) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->area!=actor.area || p->actor!=actor.actor || p->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    if(!nativeWaypointIndex(area->definition.waypointIndex) || !p->rules.character ||
        !p->rules.character->waypointIndices.contains(actor.area) || p->rules.character->waypointIndices.at(actor.area)!=area->definition.waypointIndex) return {DomainStatus::InvalidRequest,{}};
    if(menu && waypointBusy(actor.player)) return {DomainStatus::Conflict,{}};
    if(menu && nextWaypointRevision_==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto &objects=area->definition.objects; const auto object=std::find_if(objects.begin(),objects.end(),[&](const auto &o){return o.id==id && o.rule.operation==23;});
    if(object==objects.end()) return {DomainStatus::InvalidRequest,{}};
    const auto &r=object->rule;const int x=int(object->position.x)-int(p->position.x),y=int(object->position.y)-int(p->position.y);
    if(remoteRange ? x*x+y*y>*remoteRange * *remoteRange : !interactionClear(area->definition.collision,p->position,{id,object->position,object->position,r.width,r.height,float(r.range),true})) return {DomainStatus::InvalidRequest,{}};
    auto access=state_.waypoints;
    if(menu) access[actor.player]={id,actor.area,actor.areaGeneration,nextWaypointRevision_}; else access.erase(actor.player);
    transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};
    edit.waypoints=p->persistent.waypoints; edit.waypoints->try_emplace(actor.area,float(actor.tick)/25.f);
    if(menu) {
        WaypointFact fact{id,{}}; for(const auto &[region,time]:*edit.waypoints) { (void)time; fact.unlocked.push_back(region); }
        edit.facts.emplace_back(std::move(fact));
    }
    auto plan=ports_.transactions.prepare(std::move(edit)); if(!plan) return {plan.status,{}};
    auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result) {state_.waypoints.swap(access);cancel(actor.player);if(menu) ++nextWaypointRevision_;}
    return result;
}
DomainResult<> System::useSpecial(const ActorContext &actor,const Request &request,std::optional<int> remoteRange) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    if(request.kind==Kind::Npc) {
        const auto *npc=ports_.npc.find(actor,request.source.id,true);
        if(!npc || !request.destination) return {DomainStatus::InvalidRequest,{}};
        const bool east=npc->rule.code=="warriv1" && int(actor.area)==1 && int(*request.destination)==40;
        const bool west=npc->rule.code=="warriv2" && int(actor.area)==40 && int(*request.destination)==1;
        if((!east && !west) ||
            p->persistent.player.quests.at(size_t(p->persistent.difficulty)).at(questIndex(QuestId::SistersToTheSlaughter)).stage<uint32_t(SlaughterStage::AndarielSlain)) return {DomainStatus::InvalidRequest,{}};
        const auto prepared=ports_.world.request(*request.destination);if(!prepared) return {prepared.status,{}};
        Transition next{actor.area,*request.destination,*prepared.value,actor.areaGeneration,actor.sequence,npc->id,p->position,{},{},false,false,false,0,0};next.kind=Kind::Npc;
        next.npcConversation=ports_.npc.conversation(actor.player)->revision;
        state_.transitions[actor.player]=next;auto &player=ports_.players.players_.at(actor.player);player.route.clear();player.moving=false;
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(request.kind==Kind::Waypoint) {
        const auto access=state_.waypoints.find(actor.player);
        if(access==state_.waypoints.end() || access->second.source!=request.source.id || access->second.area!=actor.area || access->second.generation!=actor.areaGeneration) return {DomainStatus::InvalidRequest,{}};
        const auto dismiss=[&] {state_.waypoints.erase(actor.player);cancel(actor.player);};
        if(!waypointAccess(actor,request.source.id)) return {DomainStatus::InvalidRequest,{}};
        if(!request.destination || int(*request.destination)==0 || *request.destination==actor.area) {dismiss();return {DomainStatus::Applied,std::monostate{}};}
        if(!p->rules.character || !p->rules.character->waypointIndices.contains(*request.destination) || !p->persistent.waypoints.contains(*request.destination)) {dismiss();return {DomainStatus::InvalidRequest,{}};}
        auto prepared=ports_.world.requestWaypoint(actor,*request.destination); if(!prepared) return {prepared.status,{}};
        Transition next{actor.area,*request.destination,*prepared.value,actor.areaGeneration,actor.sequence,request.source.id,p->position,{},{},false,false,false,0,0}; next.kind=Kind::Waypoint;
        next.waypointRevision=access->second.revision;
        state_.transitions[actor.player]=next; auto &mutablePlayer=ports_.players.players_.at(actor.player); mutablePlayer.route.clear(); mutablePlayer.moving=false;
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(request.kind!=Kind::Portal) return {};
    for(const auto &[destination,portal]:state_.specialPortals) {
        const bool returning=portal.town==actor.area && portal.townId==request.source.id;
        if(!returning && !(portal.field==actor.area && portal.fieldId==request.source.id)) continue;
        (void)destination;
        if(int(portal.town)==39 && p->persistent.player.cowKingKilled.at(size_t(p->persistent.difficulty))) return {DomainStatus::Unavailable,{}};
        const auto at=returning?portal.townPosition:portal.fieldPosition;
        if((at-p->position).length()>portal.rule.range || !area->definition.collision.segment(p->position,at,{},playerMovement)) return {DomainStatus::InvalidRequest,{}};
        Transition next{actor.area,returning?portal.field:portal.town,0,actor.areaGeneration,actor.sequence,request.source.id,p->position,returning?portal.fieldPosition:portal.townPosition,{},false,false,false,0,0};next.kind=Kind::SpecialPortal;
        state_.transitions[actor.player]=next;auto &player=ports_.players.players_.at(actor.player);player.route.clear();player.moving=false;return {DomainStatus::Applied,std::monostate{}};
    }
    for(const auto &[owner,portal]:state_.portals) {
        const bool returning=portal.town==actor.area && portal.townId==request.source.id;
        if(!returning && !(portal.field==actor.area && portal.fieldId==request.source.id)) continue;
        // Party membership authority has not been implemented; ownership is mandatory.
        if(owner!=actor.player || !portal.opened) return {DomainStatus::InvalidRequest,{}};
        const auto at=returning?portal.townPosition:portal.fieldPosition;
        const int x=int(at.x)-int(p->position.x),y=int(at.y)-int(p->position.y);
        if(remoteRange ? x*x+y*y>*remoteRange * *remoteRange : ((at-p->position).length()>portal.rule.range || !area->definition.collision.segment(p->position,at,{},playerMovement))) return {DomainStatus::InvalidRequest,{}};
        Transition next{actor.area,returning?portal.field:portal.town,0,actor.areaGeneration,actor.sequence,request.source.id,p->position,returning?portal.fieldPosition:portal.townPosition,{},false,false,false,0,0}; next.kind=Kind::Portal;
        state_.transitions[actor.player]=next; auto &mutablePlayer=ports_.players.players_.at(actor.player); mutablePlayer.route.clear(); mutablePlayer.moving=false;
        return {DomainStatus::Applied,std::monostate{}};
    }
    return {DomainStatus::Stale,{}};
}
}
