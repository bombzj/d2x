#include "planning.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/travel/system.hpp"

namespace d2x::server::inventory {
DomainResult<> System::beginIdentify(const ActorContext &actor,ItemHandle handle) {
    const auto *p=ports_.players.find(actor.player);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !p->rules.character || ports_.areas.at(actor.area).generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    const auto item=p->persistent.inventory.items.find(handle.id);
    if(item==p->persistent.inventory.items.end() || item->second.revision!=handle.revision) return {DomainStatus::Stale,{}};
    const auto &source=item->second;const auto *at=std::get_if<ContainerLocation>(&source.location);
    const auto rule=p->rules.character->itemSkills.find(source.definition);
    if(!at || at->container!=p->persistent.containers.backpack || rule==p->rules.character->itemSkills.end() ||
        rule->second.action!=ItemSkillAction::Identify || rule->second.cursor<0 || rule->second.cursor>254 ||
        !(rule->second.book?source.charges:source.quantity)) return {DomainStatus::InvalidRequest,{}};
    for(const auto &[id,value]:p->persistent.inventory.items) {(void)id;const auto *location=std::get_if<ContainerLocation>(&value.location);if(location && location->container==p->persistent.containers.cursor) return {DomainStatus::Conflict,{}};}
    // Identification consumes its source only when the original 0x27 supplies a valid target.
    const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{ItemTargetingFact{source.id,rule->second.cursor,rule->second.skill}}});
    return {sent.status,sent?std::optional{std::monostate{}}:std::nullopt};
}
DomainResult<> System::useSkill(const ActorContext &actor,int skill) {
    const auto *p=ports_.players.find(actor.player);if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !p->rules.character || ports_.areas.at(actor.area).generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    for(const auto &[id,item]:p->persistent.inventory.items) {
        const auto rule=p->rules.character->itemSkills.find(item.definition);const auto *at=std::get_if<ContainerLocation>(&item.location);
        if(rule==p->rules.character->itemSkills.end() || rule->second.skill!=skill || !at || at->container!=p->persistent.containers.backpack || !(rule->second.book?item.charges:item.quantity)) continue;
        return rule->second.action==ItemSkillAction::Identify?beginIdentify(actor,{id,item.revision}):ports_.travel.createPortal(actor,ItemHandle{id,item.revision},skill);
    }
    return {DomainStatus::Unavailable,{}};
}
DomainResult<> System::identify(const ActorContext &actor, const IdentifyItem &request) {
    const auto &player = *ports_.players.find(actor.player);
    if (player.persistent.player.hp <= 0 || request.source.id == request.target.id) return {DomainStatus::InvalidRequest, {}};
    detail::Draft draft(player, *ports_.definitions, *player.rules.equipment, *player.rules.character);
    draft.storage = storageAccess(actor.player);
    const bool cube = cubeAccess(actor.player);
    const auto *source = draft.resolve(request.source), *target = draft.resolve(request.target);
    if (!source || !target) return {DomainStatus::Stale, {}};
    if (source->revision == UINT64_MAX || target->revision == UINT64_MAX) return {DomainStatus::Capacity, {}};
    const auto *from = std::get_if<ContainerLocation>(&source->location), *to = std::get_if<ContainerLocation>(&target->location);
    const auto &containers = draft.containers();
    if (!from || from->container != containers.backpack || !to || !draft.owned(*to) ||
        (to->container != containers.backpack && to->container != containers.equipment && to->container != containers.beltEquipment &&
         !(draft.storage && to->container == containers.stash) && !(cube && to->container == containers.cube)) || draft.at({containers.cursor, {}}) || target->identified)
        return {DomainStatus::InvalidRequest, {}};
    const auto rule = player.rules.character->itemSkills.find(source->definition);
    if (rule == player.rules.character->itemSkills.end() || rule->second.action!=ItemSkillAction::Identify || !(rule->second.book ? source->charges : source->quantity))
        return {DomainStatus::InvalidRequest, {}};
    auto &identified = draft.edit.inventory.items.at(target->id);
    identified.identified = true; identified.nativeFlags |= 0x10; ++identified.revision;
    draft.edit.changes.push_back({identified.id, identified.revision, ItemChangeKind::PropertiesChanged,
        identified.location, identified.location, identified.quantity});
    auto &used = draft.edit.inventory.items.at(source->id); ++used.revision;
    const bool removed = rule->second.book ? (--used.charges, false) : --used.quantity == 0;
    draft.edit.changes.push_back({used.id, used.revision, removed ? ItemChangeKind::Removed : ItemChangeKind::QuantityChanged,
        used.location, removed ? std::nullopt : std::optional<ItemLocation>{used.location}, used.quantity});
    if (removed) draft.edit.inventory.items.erase(request.source.id);
    transactions::InventoryEdit edit{actor, player.inventoryRevision, player.characterRevision,
        std::move(draft.edit.inventory), std::move(draft.edit.changes), player.persistent.player.weaponSet};
    edit.facts.emplace_back(ItemTargetingFact{request.source.id,-1});
    auto plan = ports_.transactions.prepare(std::move(edit));
    return plan ? ports_.transactions.commit(std::move(*plan.value)) : DomainResult<>{plan.status, {}};
}
}
