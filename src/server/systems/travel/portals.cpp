#include "gameplay/quest/catalog.hpp"
#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::travel {
namespace {
std::array<NativeFootprint,2> portalFootprints(const AreaStore &areas,const Portal &portal) {
    std::array<NativeFootprint,2> result{};
    if(portal.rule.collision[size_t(portal.isOpened(false)?2:1)]) result[0]=areas.rectangleFootprint(portal.field,portal.fieldPosition,portal.rule.width,portal.rule.height,portal.rule.collisionMask);
    if(portal.rule.collision[size_t(portal.isOpened(true)?2:1)]) result[1]=areas.rectangleFootprint(portal.town,portal.townPosition,portal.rule.width,portal.rule.height,portal.rule.collisionMask);
    return result;
}
template<class Key>
void publishPortalCollision(AreaStore &areas,const std::map<Key,Portal> &before,const std::map<Key,Portal> &after) {
    const auto changed=[&](const auto &map,const auto &id,const auto &portal) {
        const auto next=map.find(id);return next==map.end() || portalFootprints(areas,next->second)!=portalFootprints(areas,portal);
    };
    for(const auto &[id,portal]:before) if(changed(after,id,portal)) for(const auto &footprint:portalFootprints(areas,portal)) areas.writeFootprint(footprint,false);
    for(const auto &[id,portal]:after) if(changed(before,id,portal)) for(const auto &footprint:portalFootprints(areas,portal)) areas.writeFootprint(footprint,true);
}
}
void System::writePortalCollision(const Portal &portal,bool occupied) noexcept {
    for(const auto &footprint:portalFootprints(ports_.areas,portal)) ports_.areas.writeFootprint(footprint,occupied);
}
void System::commitSpecialPortal(SpecialPortalPlan plan) noexcept {
    for(const auto &[id,portal]:plan.next) if(!state_.specialPortals.contains(id))
        pd2PortalLandings_.erase({int(portal.field),int(portal.town),portal.rule.definition,
            int(portal.fieldPosition.x),int(portal.fieldPosition.y)});
    publishPortalCollision(ports_.areas,state_.specialPortals,plan.next);state_.specialPortals.swap(plan.next);
}
void System::closePlayerPortal(PlayerId id) {
    const auto found=state_.portals.find(id);if(found==state_.portals.end()) return;
    writePortalCollision(found->second,false);state_.portals.erase(found);
}
void System::closePortals(RegionId region) {
    std::erase_if(pd2PortalLandings_,[&](const auto &entry){return std::get<0>(entry.first)==int(region) || std::get<1>(entry.first)==int(region);});
    const auto close=[&](const auto &entry) {
        if(entry.second.field!=region && entry.second.town!=region) return false;
        writePortalCollision(entry.second,false);return true;
    };
    std::erase_if(state_.portals,[&](const auto &entry) {
        if(entry.second.field!=region) return false;
        writePortalCollision(entry.second,false);return true;
    });
    std::erase_if(state_.specialPortals,close);
}

std::optional<std::pair<RegionId,Vec>> System::nativePortalPosition(EntityId id) const {
    const auto endpoint=[&](const Portal &portal)->std::optional<std::pair<RegionId,Vec>> {
        if(id && id==portal.fieldId) return std::pair{portal.field,portal.fieldPosition};
        if(id && id==portal.townId) return std::pair{portal.town,portal.townPosition};
        return {};
    };
    for(const auto &[owner,portal]:state_.portals) {(void)owner;if(const auto at=endpoint(portal)) return at;}
    for(const auto &[key,portal]:state_.specialPortals) {(void)key;if(const auto at=endpoint(portal)) return at;}
    return {};
}
std::optional<Vec> System::portalPosition(const ActorContext &actor,EntityId id) const {
    for(const auto &[owner,portal]:state_.portals) { (void)owner;
        if(portal.field==actor.area && portal.fieldId==id) return portal.fieldPosition;
        if(portal.town==actor.area && portal.townId==id) return portal.townPosition;
    }
    for(const auto &[destination,portal]:state_.specialPortals) {(void)destination;
        if(portal.field==actor.area && portal.fieldId==id) return portal.fieldPosition;
        if(portal.town==actor.area && portal.townId==id) return portal.townPosition;
    }
    return {};
}
DomainResult<> System::createPortal(const ActorContext &actor,std::optional<ItemHandle> requested,std::optional<int> skill) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration || area->definition.town || !area->definition.portalRule || !p->rules.character) return {DomainStatus::InvalidActor,{}};
    const auto *town=ports_.areas.find(area->definition.townRegion);
    if(!town) { ports_.world.requestTown(actor); return {DomainStatus::Unavailable,{}}; }
    inventory::detail::Draft draft(*p,*p->rules.items,*p->rules.equipment,*p->rules.character);
    const ItemInstance *item=nullptr; const ItemSkillRule *rule=nullptr;
    for(const auto &[id,value]:p->persistent.inventory.items) {
        if(requested && (requested->id!=id || requested->revision!=value.revision)) continue;
        const auto found=p->rules.character->itemSkills.find(value.definition); if(found==p->rules.character->itemSkills.end() || found->second.action!=ItemSkillAction::Portal || (skill && *skill!=found->second.skill)) continue;
        const auto *at=std::get_if<ContainerLocation>(&value.location);
        // Explicit item use has already checked the open storage lease in inventory::consume.
        const bool stored = at && (at->container==p->persistent.containers.backpack ||
            (requested && (at->container==p->persistent.containers.stash || at->container==p->persistent.containers.cube || at->container==p->persistent.containers.belt)));
        if(!stored || !(found->second.book?value.charges:value.quantity)) continue;
        item=&value; rule=&found->second; break;
    }
    if(!item || item->revision==UINT64_MAX) return {DomainStatus::InvalidRequest,{}};
    if(!ports_.items.identityCapacity(2) || !ports_.events.hasCapacity(2)) return {DomainStatus::Capacity,{}};
    const auto global=p->position+area->definition.origin;
    const int column=int(std::floor(global.x)),row=int(std::floor(global.y));
    const auto sourceRoom=ports_.areas.roomAtPosition(actor.area,p->position+Vec{.5f,.5f},column,row);
    if(!sourceRoom) return {DomainStatus::Unavailable,{}};
    // CreatePortalObject uses the native spawn footprint and expfield before
    // CreateLinkPortal streams the destination's tile11 room. A prepared town
    // anchor is not evidence that its current room collision is loaded/clear.
    const auto field=ports_.areas.nativeFreeCoordinates(*sourceRoom,column,row,3,0x3e01,
        ports_.spatial.nativeCollisionSample(),std::pair{column,row},0x0c01);
    if(!field) return {DomainStatus::Unavailable,{}};
    const auto linked=ports_.world.nativeLinkPortalPosition(area->definition.townRegion);
    if(!linked) return {DomainStatus::Unavailable,{}};
    auto portals=state_.portals;
    Portal portal{actor.player,actor.actor,ports_.items.reserveIdentity(),ports_.items.reserveIdentity(),p->persistent.player.name,field->room.area,linked->area,field->position,linked->position,*area->definition.portalRule,actor.tick+uint64_t(area->definition.portalRule->openingTicks),1,false};
    portal.fieldRoom=field->room;portal.townRoom=linked->room;
    portals[actor.player]=portal;
    auto &consumed=draft.edit.inventory.items.at(item->id); ++consumed.revision;
    const bool remove=rule->book? (--consumed.charges,false):--consumed.quantity==0;
    draft.edit.changes.push_back({item->id,consumed.revision,remove?ItemChangeKind::Removed:ItemChangeKind::QuantityChanged,item->location,remove?std::nullopt:std::optional<ItemLocation>{item->location},consumed.quantity});
    const auto origin=std::get<ContainerLocation>(item->location);
    if(remove) draft.edit.inventory.items.erase(item->id);
    if(remove && origin.container==p->persistent.containers.belt) {
        const auto result=draft.compactBeltColumn(origin.cell.x);
        if(result!=DomainStatus::Applied) return {result,{}};
    }
    transactions::InventoryEdit edit{actor,p->inventoryRevision,p->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),p->persistent.player.weaponSet};
    auto plan=ports_.transactions.prepare(std::move(edit)); if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value)); if(result) {publishPortalCollision(ports_.areas,state_.portals,portals);state_.portals.swap(portals);ports_.quests.onTownPortal(actor.area);} return result;
}
DomainResult<SpecialPortalPlan> System::prepareSpecialPortal(const ActorContext &actor,SpecialPortalKind kind,uint64_t seed,std::optional<Vec> source) {
    const auto *p=ports_.players.find(actor.player);const auto *origin=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !origin || origin->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    const bool blue=kind==SpecialPortalKind::Shrine || kind==SpecialPortalKind::Andariel || kind==SpecialPortalKind::CainRescue || kind==SpecialPortalKind::Tyrael;
    const auto exists=[&](int destination) {return std::any_of(state_.specialPortals.begin(),state_.specialPortals.end(),[&](const auto &entry){return entry.second.field==actor.area && int(entry.second.town)==destination;});};
    auto rule=blue?origin->definition.portalRule:origin->definition.specialPortalRule;
    if(kind==SpecialPortalKind::ActFive || kind==SpecialPortalKind::BaalExit) {
        const auto prepared=origin->definition.questPortalRules.find(kind==SpecialPortalKind::ActFive?566:565);
        if(prepared==origin->definition.questPortalRules.end()) return {DomainStatus::Unavailable,{}};
        rule=prepared->second;
    }
    if(!rule) return {DomainStatus::Unavailable,{}};
    int level=0;const auto difficulty=size_t(p->persistent.difficulty);
    if(kind==SpecialPortalKind::Shrine) {
        if(origin->definition.town) return {DomainStatus::InvalidRequest,{}};
        level=int(origin->definition.townRegion);
    } else if(kind==SpecialPortalKind::ActFive) {
        if(int(actor.area)!=103) return {DomainStatus::InvalidRequest,{}};
        level=109;
    } else if(kind==SpecialPortalKind::AnyaTemple) {
        if(int(actor.area)!=109) return {DomainStatus::InvalidRequest,{}};
        level=121;
    } else if(kind==SpecialPortalKind::BaalExit) {
        if(int(actor.area)!=132) return {DomainStatus::InvalidRequest,{}};
        level=109;
    } else if(kind==SpecialPortalKind::Cow) {
        if(int(actor.area)!=1 || p->persistent.player.cowKingKilled.at(difficulty) || p->persistent.player.quests.at(difficulty).at(questIndex(QuestId::EveOfDestruction)).stage<questCompletionStage(QuestId::EveOfDestruction)) return {DomainStatus::InvalidRequest,{}};
        level=39;
    } else if(kind==SpecialPortalKind::Tristram) {
        if(int(actor.area)!=4) return {DomainStatus::InvalidRequest,{}};
        level=38;
    } else if(kind==SpecialPortalKind::ArcaneCanyon) {
        if(int(actor.area)!=74) return {DomainStatus::InvalidRequest,{}};
        level=46;
    } else if(kind==SpecialPortalKind::Tyrael) {
        if(int(actor.area)!=73) return {DomainStatus::InvalidRequest,{}};
        level=40;
    } else if(blue) {
        if(int(actor.area)!=(kind==SpecialPortalKind::CainRescue?38:37)) return {DomainStatus::InvalidRequest,{}};
        level=1;
    } else {
        if(int(actor.area)!=109 || difficulty!=2) return {DomainStatus::InvalidRequest,{}};
        if(kind==SpecialPortalKind::Finale) level=136;
        else {const int first=int(limitedRandom(seed,3));for(int offset=0;offset<3;++offset) if(!exists(133+(first+offset)%3)) {level=133+(first+offset)%3;break;}}
    }
    if(!level || (kind!=SpecialPortalKind::Shrine && exists(level))) return {DomainStatus::Conflict,{}};
    return prepareWorldPortal(actor.area,level,actor.tick,source.value_or(p->position),*rule,blue,false,kind==SpecialPortalKind::Tyrael);
}
DomainResult<SpecialPortalPlan> System::prepareNativePermanentPortal(RegionId region,Vec position,int destination,uint64_t tick,int definition) {
    const auto *area=ports_.areas.find(region);
    if(!area || destination<1 || destination>255) return {DomainStatus::InvalidRequest,{}};
    const auto custom=area->definition.questPortalRules.find(definition);
    const auto *rule=custom!=area->definition.questPortalRules.end()?&custom->second:
        area->definition.specialPortalRule && area->definition.specialPortalRule->definition==definition?&*area->definition.specialPortalRule:nullptr;
    if(!rule) return {DomainStatus::Unavailable,{}};
    return prepareWorldPortal(region,destination,tick,position,*rule,false,true);
}
DomainResult<> System::linkNativePortal(EntityId source,uint64_t tick) {
    if(state_.specialPortals.contains(source)) {nativeLinks_.erase(source);return {DomainStatus::Applied,std::monostate{}};}
    const auto found=ports_.objects.read().objects.find(source);
    if(found==ports_.objects.read().objects.end()) return {DomainStatus::Stale,{}};
    const auto &object=found->second;
    if(object.rule.initialization!=12 || !object.portalDestination ||
        (int(object.area)!=111 && int(object.area)!=112 && int(object.area)!=117)) return {DomainStatus::InvalidRequest,{}};
    const RegionId destination{object.portalDestination};
    const auto *area=ports_.areas.find(destination);
    if(!area) {const auto request=ports_.world.request(destination);return {request?DomainStatus::Capacity:request.status,{}};}
    const auto rule=area->definition.specialPortalRule;
    if(!rule || rule->definition!=object.definition || !area->definition.objectRules.contains(object.definition))
        return {DomainStatus::NotImplemented,{}};
    if(!ports_.items.identityCapacity(1) || ports_.objects.read().objects.size()>=65536) return {DomainStatus::Capacity,{}};
    auto position=nativeLinks_.find(source);
    if(position==nativeLinks_.end()) {
        const auto point=ports_.world.nativeLinkPortalPosition(destination);
        if(!point) return {DomainStatus::Unavailable,{}};
        const auto *target=ports_.areas.find(point->area);if(!target) return {DomainStatus::Unavailable,{}};
        position=nativeLinks_.emplace(source,NativeLinkPosition{point->area,target->generation,point->position,point->room}).first;
    }
    const auto *target=ports_.areas.find(position->second.area);
    if(!target || target->generation!=position->second.generation) {nativeLinks_.erase(position);return {DomainStatus::Stale,{}};}
    if(target->definition.act!=ports_.areas.at(object.area).definition.act) return {DomainStatus::InvalidRequest,{}};
    const auto identity=EntityId{ports_.items.identityCursor()};
    auto portals=state_.specialPortals;
    portals.emplace(source,Portal{{},{},source,identity,{},object.area,position->second.area,object.position,
        position->second.position,*rule,tick,1,true,true,{},position->second.room});
    ports_.world.retainControllerRoom(object.area,object.position,true);
    if(position->second.room) {
        const auto &origin=target->definition.origin;
        ports_.world.retainControllerRoom(position->second.area,
            Vec{float(position->second.room->x*5),float(position->second.room->y*5)}-origin,true);
    } else ports_.world.retainControllerRoom(position->second.area,position->second.position,true);
    const auto linked=ports_.objects.admit({object.definition,position->second.area,position->second.position,{},{},2,tick,position->second.room});
    if(!linked) return {linked.status,{}};
    if(*linked.value!=identity) throw std::logic_error("Unexpected native linked portal identity");
    publishPortalCollision(ports_.areas,state_.specialPortals,portals);state_.specialPortals.swap(portals);
    nativeLinks_.erase(position);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<SpecialPortalPlan> System::prepareWorldPortal(RegionId region,int level,uint64_t tick,Vec source,const PortalRule &rule,bool blue,bool exactSource,bool durielOpening) {
    const auto *origin=ports_.areas.find(region);if(!origin) return {DomainStatus::Unavailable,{}};
    const auto *destination=ports_.areas.find(RegionId(level));
    if(!destination) {const auto request=ports_.world.request(RegionId(level));return {request?DomainStatus::Capacity:request.status,{}};}
    if(!(blue?destination->definition.portalRule:destination->definition.specialPortalRule)) return {DomainStatus::Unavailable,{}};
    const auto freePoint=[&](const AreaState &area,Vec center)->std::optional<Vec> {
        for(int radius=0;radius<=4;++radius) for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            const Vec point{std::floor(center.x)+float(x),std::floor(center.y)+float(y)};
            if(!area.definition.collision.walkable(point,playerMovement)) continue;
            bool occupied=false;
            for(const auto &[level,portal]:state_.specialPortals) {(void)level;const auto at=portal.field==area.definition.id?std::optional{portal.fieldPosition}:portal.town==area.definition.id?std::optional{portal.townPosition}:std::nullopt;if(at && (*at-point).length()<2) occupied=true;}
            for(const auto &[id,item]:ports_.items.read().world.items) {(void)id;if(const auto *ground=std::get_if<GroundLocation>(&item.location);ground && ground->region==area.definition.id && (ground->position-point).length()<1) occupied=true;}
            if(!occupied) return point;
        }
        return {};
    };
    if(!ports_.items.identityCapacity(2)) return {DomainStatus::Capacity,{}};
    auto arrival=exactSource?std::optional<Vec>{}:destination->definition.portalArrival;
    if(!arrival && destination->definition.town) arrival=destination->definition.spawn;
    if(!arrival && !exactSource && !durielOpening) return {DomainStatus::Unavailable,{}};
    // Permanent native portals use the supplied subtile without a free-cell search.
    const auto from=exactSource?std::optional<Vec>{Vec{std::floor(source.x),std::floor(source.y)}}:freePoint(*origin,source);
    if(exactSource && (!ports_.areas.roomAtPosition(region,*from,int(from->x+origin->definition.origin.x),int(from->y+origin->definition.origin.y)) ||
        origin->definition.act!=destination->definition.act)) return {DomainStatus::Unavailable,{}};
    std::optional<world::QuestPosition> nativeTo;
    if(exactSource && origin->definition.pd2Map) {
        const auto key=std::tuple{int(region),level,rule.definition,int(from->x),int(from->y)};
        auto job=pd2PortalLandings_.find(key);
        if(job==pd2PortalLandings_.end()) {
            const auto anchor=ports_.world.nativeActSpawnLocation(RegionId(level),11);
            if(!anchor) return {DomainStatus::Unavailable,{}};
            job=pd2PortalLandings_.emplace(key,Pd2PortalLanding{{RegionId(level),destination->generation,*anchor,{}},{}}).first;
        }
        auto &cursor=job->second;
        if(cursor.anchor.generation!=destination->generation) {pd2PortalLandings_.erase(job);return {DomainStatus::Stale,{}};}
        ports_.world.activate(cursor.anchor.area,cursor.anchor.position);
        if(!ports_.areas.loaded(cursor.anchor.area,cursor.anchor.position)) return {DomainStatus::PendingContent,{}};
        if(!cursor.landing) {
            const auto start=ports_.world.nativeActSpawnLocationExAt(cursor.anchor.area,cursor.anchor.position,3);
            if(!start) return {DomainStatus::Unavailable,{}};
            const auto linked=ports_.world.nativeLinkPortalPositionFrom(*start,durielOpening);
            if(!linked) return {DomainStatus::Unavailable,{}};
            cursor.landing=NativeLinkPosition{linked->area,ports_.areas.at(linked->area).generation,linked->position,linked->room};
        }
        const auto &landing=*cursor.landing;
        ports_.world.activate(landing.area,landing.position);
        if(!ports_.areas.loaded(landing.area,landing.position)) return {DomainStatus::PendingContent,{}};
        nativeTo=world::QuestPosition{landing.area,landing.position,landing.room};
    } else if(exactSource || durielOpening) nativeTo=ports_.world.nativeLinkPortalPosition(RegionId(level),durielOpening);
    const auto to=exactSource || durielOpening?(nativeTo?std::optional<Vec>{nativeTo->position}:std::nullopt):freePoint(*destination,*arrival);
    if(!from || !to) return {DomainStatus::Conflict,{}};
    SpecialPortalPlan plan{state_.specialPortals};
    Portal portal{{},{},ports_.items.reserveIdentity(),ports_.items.reserveIdentity(),{},region,nativeTo?nativeTo->area:RegionId(level),*from,*to,rule,tick,1,true,true};
    if(rule.openingTicks>0) {portal.opened=false;portal.ready=tick+uint64_t(rule.openingTicks);portal.linkedOpened=true;}
    if(nativeTo) portal.townRoom=nativeTo->room;
    plan.next.emplace(portal.fieldId,std::move(portal));return {DomainStatus::Applied,std::move(plan)};
}

}
