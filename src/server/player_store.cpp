#include "player_store.hpp"
#include "area_store.hpp"
#include "server/systems/inventory/eligibility.hpp"
#include "gameplay/items/weapon_registration.hpp"
#include "gameplay/items/inventory_order.hpp"
#include <stdexcept>
#include <algorithm>
#include <utility>

namespace d2x::server {
void PlayerStore::admit(PlayerId id, CharacterDefinition definition,
                        PersistentCharacter persistent, AreaStore &areas, const PreparedRules &rules,uint64_t tick,uint64_t unitSeed) {
    if (!id.value || players_.contains(id))
        throw std::runtime_error("Invalid or duplicate admitted player");
    PlayerState player;
    for(uint8_t slot=0;slot<8;++slot)
        if(std::none_of(players_.begin(),players_.end(),[&](const auto &entry) {return entry.second.targetNodeIndex==slot;})) {
            player.targetNodeIndex=slot;break;
        }
    if(player.targetNodeIndex==11) throw std::runtime_error("Native player target groups exhausted");
    player.player = id; player.actor = persistent.player.id;
    player.combatRandom=unitSeed;
    if (!player.actor) throw std::runtime_error("Game admission requires a character");
    player.area = persistent.lastRegion;
    player.position = areas.at(player.area).definition.spawn;
    player.persistent = std::move(persistent);
    for (auto &corpse : player.persistent.corpses) { corpse.region = player.area; corpse.position = player.position; corpse.recoverableExperience = 0; }
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
    players_.emplace(id, std::move(player));
    areas.publishRoomUnit(*room);
}
}
