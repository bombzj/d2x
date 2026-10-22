#include "player_store.hpp"
#include "area_store.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/inventory/eligibility.hpp"
#include "gameplay/items/weapon_registration.hpp"
#include "gameplay/items/inventory_order.hpp"
#include <stdexcept>
#include <algorithm>
#include <utility>
#include <cmath>

namespace d2x::server {
void PlayerStore::admit(PlayerId id, CharacterDefinition definition,
                        PersistentCharacter persistent, AreaStore &areas, const PreparedRules &rules,uint64_t tick,uint64_t unitSeed,
                        RoomPoint arrival,const spatial::System &spatial,world::System &world) {
    if (!id.value || players_.contains(id))
        throw std::runtime_error("Invalid or duplicate admitted player");
    if (persistent.player.hardcoreDead || (persistent.player.hardcore && persistent.player.hp <= 0))
        throw std::runtime_error("Hardcore character is dead");
    PlayerState player;
    for(uint8_t slot=0;slot<8;++slot)
        if(std::none_of(players_.begin(),players_.end(),[&](const auto &entry) {return entry.second.targetNodeIndex==slot;})) {
            player.targetNodeIndex=slot;break;
        }
    if(player.targetNodeIndex==11) throw std::runtime_error("Native player target groups exhausted");
    player.player = id; player.actor = persistent.player.id;
    player.combatRandom=unitSeed;
    if (!player.actor) throw std::runtime_error("Game admission requires a character");
    player.area = arrival.room.area;
    player.position = arrival.position;
    player.persistent = std::move(persistent);
    // Clients::sub_6FC31EF0 -> Player::sub_6FC7BFC0 restores saved bodies
    // through size 2 / 0x9c19 / increment 4, including earlier restored bodies.
    const auto sample=spatial.nativeCollisionSample();
    const auto origin=areas.at(player.area).definition.origin;
    const int playerX=int(std::floor(player.position.x+origin.x+.5f)),playerY=int(std::floor(player.position.y+origin.y+.5f));
    std::vector<Vec> restored;
    for (auto &corpse : player.persistent.corpses) {
        const auto collision=[&](RoomView room,int32_t x,int32_t y)->uint16_t {
            uint16_t mask=sample(room,x,y);
            if(player.persistent.player.hp>0) mask|=nativeCollisionFootprint(NativeCollisionPattern::SmallUnit,x-playerX,y-playerY,0x80);
            for(const auto at:restored) if(std::abs(x-int(at.x))<=1 && std::abs(y-int(at.y))<=1) mask|=0x8000;
            return mask;
        };
        const auto spawn=world.nativeActSpawnLocationEx(player.area,0,2,collision);
        if(!spawn || !spawn->room) throw std::runtime_error("No native saved-corpse spawn room");
        const auto global=spawn->position+areas.at(spawn->area).definition.origin;
        const auto point=areas.nativeFreeCoordinates(*spawn->room,int(global.x),int(global.y),2,0x9c19,collision,{},0,false,50,4);
        if(!point) throw std::runtime_error("No native saved-corpse entry position");
        corpse.region=point->room.area;corpse.position=point->position;corpse.recoverableExperience=0;
        restored.push_back(point->position+areas.at(point->room.area).definition.origin);
    }
    player.definition = std::move(definition);
    player.rules = rules;
    if (player.definition.walkVelocity <= 0 || player.definition.runVelocity <= 0)
        throw std::runtime_error("Missing prepared movement rules");
    if (!rules.items || !rules.equipment || !rules.character)
        throw std::runtime_error("Game admission requires character and equipment rules");
    InventoryState emptyInventory;
    emptyInventory.containers=player.persistent.inventory.containers;
    emptyInventory.containers.at(player.persistent.containers.equipment).registeredWeapon={};
    std::vector<ItemChange> loadedItems;
    for(const auto itemId:inventoryItemOrder(player.persistent.inventory)) {
        const auto &item=player.persistent.inventory.items.at(itemId);
        loadedItems.push_back({itemId,item.revision,ItemChangeKind::Created,{},item.location,item.quantity});
    }
    const auto registeredInsertions=prepareWeaponRegistration(emptyInventory,player.persistent.inventory,*rules.items,
        player.persistent.containers.equipment,player.persistent.player.weaponSet,player.persistent.player.weaponSet,loadedItems);
    player.transient.sources.emplace();
    player.totals=attributes::calculate(player.definition,player.persistent,*rules.items,*rules.equipment,*rules.character,
        {},{}, {},player.transient.statSources(),nullptr,areas.statTime(player.area),false,{},registeredInsertions);
    refreshItemAuraTimers(player.totals.itemAuraTimers,player.totals.itemAuraChanges,tick);
    player.totals.sourceRevision = player.characterRevision;
    std::vector<ItemChange> admissionChanges;
    inventory::synchronizeEquipment(player.persistent, player.totals, *rules.equipment, admissionChanges);
    auto &record = player.persistent.player;
    record.hp = std::clamp(record.hp, Fixed8{}, player.totals.character.maxLifeFixed);
    record.mana = std::clamp(record.mana, Fixed8{}, player.totals.character.maxManaFixed);
    record.stamina = std::clamp(record.stamina, Fixed8{}, player.totals.character.maxStaminaFixed);
    auto room=areas.prepareRoomUnit(player.actor,0,player.area,player.position);
    if(!room) throw std::runtime_error("Missing native player entry room");
    players_.emplace(id, std::move(player));
    areas.publishRoomUnit(*room);
    refreshCollision(id,areas);
    publishCorpseCollision(areas,{},players_.at(id).persistent.corpses);
}
void PlayerStore::refreshCollision(PlayerId id,AreaStore &areas) const noexcept {
    const auto *player=find(id);if(!player) std::terminate();
    NativeFootprint body;
    if(player->collisionActive) body=player->persistent.player.hp>0?
        areas.patternFootprint(player->area,player->position,NativeCollisionPattern::SmallUnit,0x80):
        areas.rectangleFootprint(player->area,player->position,3,3,0x8000);
    areas.setUnitCollision(player->actor,body,{},player->persistent.player.hp>0);
}
void PlayerStore::publishCorpseCollision(AreaStore &areas,std::span<const PlayerCorpse> before,std::span<const PlayerCorpse> after) noexcept {
    const auto changed=[](const auto &list,const auto &corpse) {
        return std::none_of(list.begin(),list.end(),[&](const auto &other) {
            return corpse.id==other.id && corpse.region==other.region && corpse.position.x==other.position.x && corpse.position.y==other.position.y;
        });
    };
    for(const auto &corpse:before) if(changed(after,corpse)) areas.writeFootprint(areas.rectangleFootprint(corpse.region,corpse.position,3,3,0x8000),false);
    for(const auto &corpse:after) if(changed(before,corpse)) areas.writeFootprint(areas.rectangleFootprint(corpse.region,corpse.position,3,3,0x8000),true);
}
}
