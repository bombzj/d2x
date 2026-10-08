#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
namespace d2x::server::travel {
std::optional<Vec> System::portalPosition(const ActorContext &actor,EntityId id) const {
    for(const auto &[owner,portal]:state_.portals) { (void)owner;
        if(portal.field==actor.area && portal.fieldId==id) return portal.fieldPosition;
        if(portal.town==actor.area && portal.townId==id) return portal.townPosition;
    }
    return {};
}
DomainResult<> System::createPortal(const ActorContext &actor,std::optional<ItemHandle> requested,std::optional<int> skill) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration || area->definition.town || !area->definition.portalRule || !p->rules.character) return {DomainStatus::InvalidActor,{}};
    if(int(actor.area)==120) return {}; // Ancients reset is a separate quest authority.
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
    const auto result=ports_.transactions.commit(std::move(*plan.value)); if(result) state_.portals.swap(portals); return result;
}
}
