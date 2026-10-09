#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/quests/system.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x::server::travel {
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
    if(!town->definition.portalArrival || !town->definition.collision.walkable(*town->definition.portalArrival,playerMovement)) return {DomainStatus::Unavailable,{}};
    inventory::detail::Draft draft(*p,*p->rules.items,*p->rules.equipment,*p->rules.character);
    const ItemInstance *item=nullptr; const ItemSkillRule *rule=nullptr;
    for(const auto &[id,value]:p->persistent.inventory.items) {
        if(requested && (requested->id!=id || requested->revision!=value.revision)) continue;
        const auto found=p->rules.character->itemSkills.find(value.definition); if(found==p->rules.character->itemSkills.end() || found->second.action!=ItemSkillAction::Portal || (skill && *skill!=found->second.skill)) continue;
        const auto *at=std::get_if<ContainerLocation>(&value.location); if(!at || at->container!=p->persistent.containers.backpack || !(found->second.book?value.charges:value.quantity)) continue;
        item=&value; rule=&found->second; break;
    }
    if(!item || item->revision==UINT64_MAX) return {DomainStatus::InvalidRequest,{}};
    const auto field=area->definition.collision.nearest(p->position,playerMovement);
    if((field-p->position).length()>5 || !area->definition.collision.walkable(field,playerMovement)) return {DomainStatus::Unavailable,{}};
    auto portals=state_.portals;
    Portal portal{actor.player,actor.actor,ports_.items.reserveIdentity(),ports_.items.reserveIdentity(),p->persistent.player.name,actor.area,area->definition.townRegion,field,*town->definition.portalArrival,*area->definition.portalRule,actor.tick+uint64_t(area->definition.portalRule->openingTicks),1,false};
    portals[actor.player]=portal;
    auto &consumed=draft.edit.inventory.items.at(item->id); ++consumed.revision;
    const bool remove=rule->book? (--consumed.charges,false):--consumed.quantity==0;
    draft.edit.changes.push_back({item->id,consumed.revision,remove?ItemChangeKind::Removed:ItemChangeKind::QuantityChanged,item->location,remove?std::nullopt:std::optional<ItemLocation>{item->location},consumed.quantity});
    if(remove) draft.edit.inventory.items.erase(item->id);
    transactions::InventoryEdit edit{actor,p->inventoryRevision,p->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),p->persistent.player.weaponSet};
    auto plan=ports_.transactions.prepare(std::move(edit)); if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value)); if(result) {state_.portals.swap(portals);ports_.quests.onTownPortal(actor.area);} return result;
}
DomainResult<SpecialPortalPlan> System::prepareSpecialPortal(const ActorContext &actor,SpecialPortalKind kind,uint64_t seed,std::optional<Vec> source) {
    const auto *p=ports_.players.find(actor.player);const auto *origin=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !origin || origin->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    const bool blue=kind==SpecialPortalKind::Andariel || kind==SpecialPortalKind::CainRescue || kind==SpecialPortalKind::Tyrael;
    const auto exists=[&](int destination) {return std::any_of(state_.specialPortals.begin(),state_.specialPortals.end(),[&](const auto &entry){return entry.second.field==actor.area && int(entry.second.town)==destination;});};
    auto rule=blue?origin->definition.portalRule:origin->definition.specialPortalRule;
    if(kind==SpecialPortalKind::ActFive || kind==SpecialPortalKind::BaalExit) {
        const auto prepared=origin->definition.questPortalRules.find(kind==SpecialPortalKind::ActFive?566:565);
        if(prepared==origin->definition.questPortalRules.end()) return {DomainStatus::Unavailable,{}};
        rule=prepared->second;
    }
    if(!rule) return {DomainStatus::Unavailable,{}};
    int level=0;const auto difficulty=size_t(p->persistent.difficulty);
    if(kind==SpecialPortalKind::ActFive) {
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
    if(!level || exists(level)) return {DomainStatus::Conflict,{}};
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
    auto arrival=destination->definition.portalArrival;
    if(!arrival && destination->definition.town) arrival=destination->definition.spawn;
    if(!arrival) return {DomainStatus::Unavailable,{}};
    const auto from=freePoint(*origin,source.value_or(p->position)),to=freePoint(*destination,*arrival);
    if(!from || !to) return {DomainStatus::Conflict,{}};
    if(!ports_.items.identityCapacity(2)) return {DomainStatus::Capacity,{}};
    SpecialPortalPlan plan{state_.specialPortals};
    Portal portal{{},{},ports_.items.reserveIdentity(),ports_.items.reserveIdentity(),{},actor.area,RegionId(level),*from,*to,*rule,actor.tick,1,true,true};
    plan.next.emplace(portal.fieldId,std::move(portal));return {DomainStatus::Applied,std::move(plan)};
}

}
