#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "gameplay/maps/pd2/region_program.hpp"
#include "gameplay/maps/pd2/event_program.hpp"
#include "gameplay/items/native_sources.hpp"
#include "gameplay/items/quality.hpp"
#include "gameplay/quest/catalog.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::travel {
namespace {
bool requiresMapController(int level) {
    // PvP admission remains a separate game mode.
    switch(level) {
    case 157:case 159:return true;
    default:return false;
    }
}
bool availableMapPrograms(const pd2::MapProperties &properties) {
    // Current 17d5 stores 7/8/9/15, but neither 389510 nor its global-table
    // readers consume them. Preserve them without inventing name-based loot.
    for(const auto &program:properties.global) if(program.key.id>15) return false;
    return true;
}
}
DomainResult<> System::createPd2Map(const ActorContext &actor,ItemHandle requested) {
    if(pd2Cancelling_) {
        discardPd2MapPreparation();
        if(pd2Cancelling_) return {DomainStatus::Unavailable,{}};
    }
    const auto result=finishPd2Map(actor,requested);
    if(isDeferred(result.status) && state_.pd2Map && !state_.pd2MapPortal) {
        // World preparation is asynchronous. Keep the original item revision
        // and authority binding; a single UseItem must finish without a resend.
        state_.pendingPd2Map=actor;
        return {DomainStatus::Applied,{}};
    }
    if(!result && !isDeferred(result.status) && state_.pd2Map && state_.pd2Map->owner==actor.actor &&
       state_.pd2Map->item.id==requested.id && state_.pd2Map->item.revision==requested.revision)
        discardPd2MapPreparation();
    return result;
}
void System::discardPd2MapPreparation() {
    if(state_.pd2Map && !state_.pd2MapPortal) {
        pd2Cancelling_=true;
        state_.pendingPd2Map.reset();
        if(!ports_.world.cancelPd2Preparation(state_.pd2Map)) return;
        state_.pd2Map.reset();pd2OpeningPortal_.reset();pd2Cancelling_=false;
    }
}
void System::advancePd2Map(TickContext tick) {
    if(pd2Cancelling_) {discardPd2MapPreparation();return;}
    if(!state_.pendingPd2Map) return;
    auto actor=*state_.pendingPd2Map;actor.tick=tick.tick;
    const auto result=finishPd2Map(actor,state_.pd2Map->item);
    if(!isDeferred(result.status)) {
        state_.pendingPd2Map.reset();
        if(!result) discardPd2MapPreparation();
    }
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
    const bool scene=pd2::fixedMapController(rule->second.level)>=41 && pd2::fixedMapController(rule->second.level)<=45;
    if(int(actor.area)!=109 || player->persistent.difficulty!=2 || (!scene && player->persistent.player.level<80) ||
       player->persistent.player.quests.at(2).at(questIndex(QuestId::RiteOfPassage)).stage<questCompletionStage(QuestId::RiteOfPassage))
        return {DomainStatus::InvalidRequest,{}};
    if(scene) {
        const auto global=player->position+origin->definition.origin;
        const int x=int(global.x)-0x13d2,y=int(global.y)-0x13fa;
        if(x*x+y*y>=150) return {DomainStatus::InvalidRequest,{}};
    }
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
        candidate->initSeed=ports_.settings.mapSeed;
        candidate->properties=pd2::prepareMapProperties(catalog->properties,raw);
        if(!availableMapPrograms(candidate->properties)) return {DomainStatus::NotImplemented,{}};
        candidate->quality=uint8_t(std::find(nativeItemQualities.begin()+1,nativeItemQualities.end(),item->quality)-nativeItemQualities.begin());
        candidate->controller=pd2::selectMapController(rule->second.level,candidate->quality,candidate->properties.forceEvent,ports_.pd2Random);
        if(scene) {
            const auto difficulty=values.get({player->rules.character->stats->names.at("uber_difficulty"),0});
            if(difficulty<0 || difficulty>2) return {DomainStatus::InvalidRequest,{}};
            candidate->bossDifficulty=uint8_t(difficulty);
            for(const auto &[id,other]:ports_.players.all()) {
                (void)id;if(!other.entered || int(other.area)!=109 || other.persistent.player.hp<=0) continue;
                const auto global=other.position+origin->definition.origin;
                const int x=int(global.x)-0x13d2,y=int(global.y)-0x13fa;
                if(x*x+y*y<150) ++candidate->participants;
            }
            candidate->properties.monsters.clear();candidate->properties.players.clear();candidate->properties.global.clear();
            if(candidate->controller!=41) candidate->destination.portal=587;
        } else appendMapGlobalUnitProperties(candidate->properties);
        map=std::move(candidate);
    }
    const auto portalRule=origin->definition.questPortalRules.find(map->destination.portal);
    if(portalRule==origin->definition.questPortalRules.end()) return {DomainStatus::Unavailable,{}};
    // Preserve the selected controller across asynchronous content/capacity
    // waits. A pending UseItem never consumes another selector CRT sequence.
    state_.pd2Map=map;
    const auto prepared=ports_.world.request(RegionId(map->destination.level),map);
    if(!prepared) return {prepared.status,{}};
    state_.pd2Map=map;
    if(!ports_.areas.find(RegionId(map->destination.level))) return {DomainStatus::PendingContent,{}};
    if(!pd2OpeningPortal_) {
        const auto source=ports_.world.nativeFreeSpace(actor.area,player->position,player->position,1);
        if(!source || source->area!=actor.area) return {DomainStatus::Unavailable,{}};
        auto portals=prepareWorldPortal(actor.area,map->destination.level,actor.tick,source->position,portalRule->second,false,true);
        if(!portals) return {portals.status,{}};
        pd2OpeningPortal_=std::move(portals.value->next.rbegin()->second);
    }
    // Keep the chosen native landing and allocated IDs across output pressure.
    // Only cache this portal: replaying a whole old map would erase unrelated
    // portals created or closed while the inventory transaction was deferred.
    SpecialPortalPlan portals{state_.specialPortals};
    portals.next.emplace(pd2OpeningPortal_->fieldId,*pd2OpeningPortal_);
    auto &consumed=draft.edit.inventory.items.at(item->id);++consumed.revision;
    // pSpell12 only decrements a stacked normal-quality map. All other
    // successful uses return 1 to the item-use caller and remove the item.
    const bool removed=map->quality!=2 || consumed.quantity<=1;
    if(removed) consumed.quantity=0;else --consumed.quantity;
    draft.edit.changes.push_back({item->id,consumed.revision,removed?ItemChangeKind::Removed:ItemChangeKind::QuantityChanged,
        item->location,removed?std::nullopt:std::optional{item->location},consumed.quantity});
    if(removed) draft.edit.inventory.items.erase(item->id);
    transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(draft.edit.inventory),
        std::move(draft.edit.changes),player->persistent.player.weaponSet};
    const auto &portal=*pd2OpeningPortal_;
    std::optional<Vec> sceneArrival;
    std::optional<RoomView> sceneRoom;
    if(scene) {
        const auto &target=ports_.areas.at(portal.town);
        ports_.world.activate(portal.town,portal.townPosition);
        if(!ports_.areas.loaded(portal.town,portal.townPosition)) return {DomainStatus::PendingContent,{}};
        if(!ports_.quests.allowsTravel(player->persistent.player,actor.area,portal.town)) return {DomainStatus::InvalidRequest,{}};
        const auto global=portal.townPosition+target.definition.origin;
        const auto room=ports_.areas.roomAtPosition(portal.town,portal.townPosition,int(std::floor(global.x+.5f)),int(std::floor(global.y+.5f)));
        if(!room) return {DomainStatus::Unavailable,{}};
        auto exclusion=ports_.areas.excludeCollision(player->actor);
        const auto free=ports_.areas.nativeFreeCoordinates(*room,int(std::floor(global.x+.5f)),int(std::floor(global.y+.5f)),2,
            playerMovement.mask,ports_.spatial.nativeCollisionSample(playerMovement.mask),{},0,false,50,1);
        if(!free || free->room.area!=portal.town) return {DomainStatus::Unavailable,{}};
        sceneArrival=free->position;sceneRoom=free->room;edit.environmentRegion=portal.town;
        edit.facts.emplace_back(TravelFact{actor.player,actor.actor,actor.area,portal.town,target.generation,*sceneArrival,false});
    }
    Pd2MapAttributesFact report{portal.fieldId,RegionId(map->destination.level),map->destination.itemIndex,map->quality,map->properties.original};
    auto reports=state_.mapReports;reports.insert_or_assign(actor.player,portal.fieldId);
    if(!scene) edit.facts.emplace_back(report);
    for(const auto &[id,other]:ports_.players.all()) if(!scene && id!=actor.player && other.entered && ports_.areas.at(other.area).definition.act==4) {
        edit.directed.push_back({id,{report}});
        reports.insert_or_assign(id,portal.fieldId);
    }
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result) {
        ports_.world.commitPd2Preparation(map);
        state_.pd2MapPortal=portal.fieldId;state_.mapReports.swap(reports);
        const auto field=portal.field,town=portal.town;
        const auto from=portal.fieldPosition,to=portal.townPosition;
        commitSpecialPortal(std::move(portals));
        ports_.world.retainControllerRoom(field,from,true);ports_.world.retainControllerRoom(town,to,true);
        pd2OpeningPortal_.reset();
        if(scene) {
            state_.mapReports.clear();state_.pd2SceneEntrants.clear();state_.pd2SceneOpening=true;
            // The owner enters in the same accepted transaction as the map
            // cost. A failed landing cannot consume a scene with no entrants.
            state_.pd2SceneEntrants.insert(actor.player);
            state_.pd2SceneEntryRooms.insert(*sceneRoom);
            auto &owner=ports_.players.players_.at(actor.player);
            ports_.npc.close(actor.player);ports_.inventory.close(actor.player);state_.waypoints.erase(actor.player);
            owner.area=town;owner.position=*sceneArrival;owner.route.clear();owner.moving=false;owner.pathStep.reset();
            owner.movementTarget.reset();owner.movementArrival.reset();owner.routeDestination=owner.position;
            owner.routeObstacles=ports_.areas.at(town).definition.collision.obstacleRevision;
            ports_.areas.moveRoomUnit(owner.actor,town,owner.position);
            for(const auto id:ports_.players.nativeActorOrder()) {
                const auto *other=ports_.players.findActor(id);
                if(!other || !other->entered || int(other->area)!=109 || other->persistent.player.hp<=0) continue;
                const auto global=other->position+origin->definition.origin;
                const int x=int(global.x)-0x13d2,y=int(global.y)-0x13fa;if(x*x+y*y>=150) continue;
                Transition transition{other->area,town,0,origin->generation,other->locomotionSequence,state_.pd2MapPortal,other->position,to};
                transition.kind=Kind::SpecialPortal;transition.arrivalGeneration=ports_.areas.at(town).generation;transition.pd2SceneEntry=true;
                state_.transitions.insert_or_assign(other->player,transition);
                auto &moving=ports_.players.players_.at(other->player);moving.route.clear();moving.moving=false;
            }
        }
    }
    return result;
}
StepStatus System::synchronizePd2MapReports(TickContext tick) {
    if(!state_.pd2Map || !state_.pd2MapPortal) return StepStatus::Complete;
    if(state_.pd2Map->controller>=41 && state_.pd2Map->controller<=45) return StepStatus::Complete;
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
