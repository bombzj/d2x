#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "gameplay/maps/pd2/region_program.hpp"
#include "gameplay/items/native_sources.hpp"
#include "gameplay/items/quality.hpp"
#include "gameplay/quest/catalog.hpp"
#include <algorithm>

namespace d2x::server::travel {
namespace {
bool requiresMapController(int level) {
    // Current pSpell12's dedicated controller branches, plus PvP admission.
    switch(level) {
    case 137:case 157:case 159:case 161:case 168:case 176:case 180:
    case 181:case 183:case 185:case 188:case 192:case 195:case 197:return true;
    default:return false;
    }
}
bool availableMapPrograms(const pd2::MapProperties &properties) {
    if(properties.forceEvent) return false;
    for(const auto &program:properties.global) if(program.key.id>6 && (program.key.id<10 || program.key.id>14)) return false;
    return true;
}
}
DomainResult<> System::createPd2Map(const ActorContext &actor,ItemHandle requested) {
    const auto result=finishPd2Map(actor,requested);
    if(isDeferred(result.status) && state_.pd2Map && !state_.pd2MapPortal) {
        // World preparation is asynchronous. Keep the original item revision
        // and authority binding; a single UseItem must finish without a resend.
        state_.pendingPd2Map=actor;
        return {DomainStatus::Applied,{}};
    }
    return result;
}
void System::advancePd2Map(TickContext tick) {
    if(!state_.pendingPd2Map) return;
    auto actor=*state_.pendingPd2Map;actor.tick=tick.tick;
    const auto result=finishPd2Map(actor,state_.pd2Map->item);
    if(!isDeferred(result.status)) state_.pendingPd2Map.reset();
}
DomainResult<> System::finishPd2Map(const ActorContext &actor,ItemHandle requested) {
    const auto *player=ports_.players.find(actor.player);const auto *origin=ports_.areas.find(actor.area);
    if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area ||
       player->persistent.player.hp<=0 || !origin || origin->generation!=actor.areaGeneration ||
       !player->rules.character || !player->rules.character->maps) return {DomainStatus::InvalidActor,{}};
    const auto &catalog=player->rules.character->maps;
    inventory::detail::Draft draft(*player,*player->rules.items,*player->rules.equipment,*player->rules.character);
    const auto *item=draft.resolve(requested);
    if(!item || item->revision==UINT64_MAX) return {DomainStatus::Stale,{}};
    const auto rule=catalog->items.find(item->definition);
    if(rule==catalog->items.end() || !item->quantity) return {DomainStatus::InvalidRequest,{}};
    const auto *location=std::get_if<ContainerLocation>(&item->location);
    if(!location || !draft.owned(*location) || draft.at({draft.containers().cursor,{}})) return {DomainStatus::InvalidRequest,{}};
    if(requiresMapController(rule->second.level)) return {DomainStatus::NotImplemented,{}};
    if(int(actor.area)!=109 || player->persistent.difficulty!=2 || player->persistent.player.level<80 ||
       player->persistent.player.quests.at(2).at(questIndex(QuestId::RiteOfPassage)).stage<questCompletionStage(QuestId::RiteOfPassage))
        return {DomainStatus::InvalidRequest,{}};
    if(state_.pd2Map && (state_.pd2Map->owner!=actor.actor || state_.pd2Map->item.id!=requested.id || state_.pd2Map->item.revision!=requested.revision))
        return {DomainStatus::Conflict,{}};
    auto map=state_.pd2Map;
    if(!map) {
        EquipmentContribution contribution;contribution.item=item->id;
        contribution.stats=player->rules.equipment->at(item->id,player->persistent.player.level).stats;
        const auto sources=prepareNativeEquipmentSources(*player->rules.items,*player->rules.character->stats,
            std::span(&contribution,1),{},[&](EntityId)->const ItemInstance &{return *item;});
        const auto &root=*sources.items.at(item->id);
        const auto values=evaluateStoredStatList(*player->rules.character->stats,root);
        std::vector<StatValue> raw;for(const auto &[key,value]:values.values) raw.emplace_back(key,value);
        auto candidate=std::make_shared<pd2::MapInstance>();candidate->owner=actor.actor;candidate->item=requested;
        candidate->catalog=catalog;candidate->destination=rule->second;
        candidate->properties=pd2::prepareMapProperties(catalog->properties,raw);
        if(!availableMapPrograms(candidate->properties)) return {DomainStatus::NotImplemented,{}};
        appendMapGlobalUnitProperties(candidate->properties);
        candidate->quality=uint8_t(std::find(nativeItemQualities.begin()+1,nativeItemQualities.end(),item->quality)-nativeItemQualities.begin());
        map=std::move(candidate);
    }
    const auto prepared=ports_.world.request(RegionId(map->destination.level),map);
    if(!prepared) return {prepared.status,{}};
    state_.pd2Map=map;
    if(!ports_.areas.find(RegionId(map->destination.level))) return {DomainStatus::PendingContent,{}};
    const auto portalRule=origin->definition.questPortalRules.find(map->destination.portal);
    if(portalRule==origin->definition.questPortalRules.end()) return {DomainStatus::Unavailable,{}};
    const auto source=ports_.world.nativeFreeSpace(actor.area,player->position,player->position,1);
    if(!source || source->area!=actor.area) return {DomainStatus::Unavailable,{}};
    auto portals=prepareWorldPortal(actor.area,map->destination.level,actor.tick,source->position,portalRule->second,false,true);
    if(!portals) return {portals.status,{}};
    auto &consumed=draft.edit.inventory.items.at(item->id);++consumed.revision;--consumed.quantity;
    const bool removed=!consumed.quantity;
    draft.edit.changes.push_back({item->id,consumed.revision,removed?ItemChangeKind::Removed:ItemChangeKind::QuantityChanged,
        item->location,removed?std::nullopt:std::optional{item->location},consumed.quantity});
    if(removed) draft.edit.inventory.items.erase(item->id);
    transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(draft.edit.inventory),
        std::move(draft.edit.changes),player->persistent.player.weaponSet};
    const auto &portal=portals.value->next.rbegin()->second;
    Pd2MapAttributesFact report{portal.fieldId,RegionId(map->destination.level),map->destination.itemIndex,map->quality,map->properties.original};
    auto reports=state_.mapReports;reports.insert_or_assign(actor.player,portal.fieldId);
    edit.facts.emplace_back(report);
    for(const auto &[id,other]:ports_.players.all()) if(id!=actor.player && other.entered && ports_.areas.at(other.area).definition.act==4) {
        edit.directed.push_back({id,{report}});
        reports.insert_or_assign(id,portal.fieldId);
    }
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result) {
        state_.pd2MapPortal=portal.fieldId;state_.mapReports.swap(reports);
        const auto field=portal.field,town=portal.town;
        const auto from=portal.fieldPosition,to=portal.townPosition;
        commitSpecialPortal(std::move(*portals.value));
        ports_.world.retainControllerRoom(field,from,true);ports_.world.retainControllerRoom(town,to,true);
    }
    return result;
}
StepStatus System::synchronizePd2MapReports(TickContext tick) {
    if(!state_.pd2Map || !state_.pd2MapPortal) return StepStatus::Complete;
    std::erase_if(state_.mapReports,[&](const auto &entry) {
        const auto *player=ports_.players.find(entry.first);return !player || !player->entered;
    });
    const auto &map=*state_.pd2Map;
    bool blocked=false;
    for(const auto &[id,player]:ports_.players.all()) {
        if(!player.entered || ports_.areas.at(player.area).definition.act!=4 || state_.mapReports.contains(id)) continue;
        // Allocate the receipt before publishing, so an accepted report cannot
        // be replayed by an allocation failure or reliable-output backpressure.
        std::map<PlayerId,EntityId> receipt;receipt.emplace(id,state_.pd2MapPortal);
        const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Player,id,player.area},
            {Pd2MapAttributesFact{state_.pd2MapPortal,RegionId(map.destination.level),map.destination.itemIndex,map.quality,map.properties.original}}});
        if(!sent) {blocked=true;continue;}
        state_.mapReports.merge(receipt);
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
