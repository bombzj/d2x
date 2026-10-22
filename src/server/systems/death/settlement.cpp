#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x::server::death {
DomainResult<> System::settle(const ActorContext &actor) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || player->actor != actor.actor || player->persistent.player.hp > 0 || !player->rules.character) return {DomainStatus::InvalidActor, {}};
    inventory::detail::Draft draft(*player, *player->rules.items, *player->rules.equipment, *player->rules.character);
    auto corpses = player->persistent.corpses;
    auto world = ports_.items.read();
    if (world.revision == UINT64_MAX || !ports_.transactions.hasOutputCapacity()) return {DomainStatus::Capacity, {}};
    auto equipment = std::make_shared<EquipmentRules>(*player->rules.equipment);
    std::vector<DomainFact> drops;
    PlayerCorpse corpse; corpse.owner = actor.actor; corpse.region = actor.area; corpse.position = player->position; corpse.look = player->look;
    if (corpses.size() < 16) {
        if (!ports_.items.identityCapacity(2)) return {DomainStatus::Capacity, {}};
        corpse.id = ports_.items.reserveIdentity(); corpse.items = ports_.items.reserveIdentity();
        draft.edit.inventory.containers.emplace(corpse.items, ContainerState{corpse.items, {actor.actor, ContainerKind::Corpse, int(EquipmentSlot::Count) + 1, 1}});
    }
    const auto drop = [&](EntityId id) -> bool {
        auto &item = draft.edit.inventory.items.at(id);
        const auto position = ports_.items.placement({actor.area, player->position}, world.world);
        if (!position || item.revision == UINT64_MAX || world.world.items.size() >= 4096) return false;
        draft.edit.changes.push_back({id, item.revision + 1, ItemChangeKind::Removed, item.location, {}, 0});
        item.location = *position; ++item.revision;
        drops.emplace_back(GroundDropFact{item});
        world.world.items.emplace(id, item); world.equipment.items.insert_or_assign(id, equipment->items.at(id));
        equipment->items.erase(id); draft.edit.inventory.items.erase(id); return true;
    };
    for (int slot = 0; slot <= int(EquipmentSlot::Count); ++slot) {
        const auto id = slot == int(EquipmentSlot::Count) ? draft.at({draft.containers().cursor, {}}) : draft.equipped(EquipmentSlot(slot));
        if (!id) continue;
        if (corpse.items) {
            if (const auto result = draft.move(id, {corpse.items, {slot, 0}}); result != DomainStatus::Applied) return {result, {}};
        } else if (!drop(id)) return {DomainStatus::Capacity, {}};
    }
    std::vector<EntityId> excess;
    for (const auto &[id, item] : draft.edit.inventory.items)
        if (const auto *at = std::get_if<ContainerLocation>(&item.location); at && at->container == draft.containers().belt && at->cell.y > 0) excess.push_back(id);
    for (const auto id : excess) {
        if (const auto destination = draft.space(id, draft.containers().backpack)) {
            if (const auto result = draft.move(id, *destination); result != DomainStatus::Applied) return {result, {}};
        } else if (!drop(id)) return {DomainStatus::Capacity, {}};
    }
    draft.edit.inventory.containers.at(draft.containers().belt).spec.rows = 1;
    auto record = player->persistent.player;
    if (record.hardcore) record.hardcoreDead = true;
    const auto transition=state_.transitions.find(actor.actor);
    const auto message=transition==state_.transitions.end()?PlayerMessageFact{}:transition->second.message;
    const bool pvp=!message.other.empty();
    const uint64_t total = uint64_t(record.gold) + record.bankGold;
    auto penalty = total * unsigned(std::min(record.level, 20)) / 100;
    if (ports_.settings.singlePlayer) {
        const auto protectedGold = uint64_t(record.level) * 500;
        penalty = std::min<uint64_t>(record.gold, std::min(penalty, total > protectedGold ? total - protectedGold : 0));
    }
    unsigned dropGold=0;
    if(pvp) {
        // Player::ApplyDeathPenalty drops only the penalty for PvP; it does
        // not destroy it and then drop the victim's remaining carried gold.
        if(penalty>record.gold) {record.bankGold-=unsigned(penalty-record.gold);record.gold=unsigned(penalty);}
        dropGold=unsigned(penalty);record.gold-=dropGold;
    } else {
        if (penalty > record.gold) { record.bankGold -= unsigned(penalty - record.gold); record.gold = 0; }
        else record.gold -= unsigned(penalty);
        dropGold=record.gold;record.gold=0;
    }
    // Native death gold is a normal compact item. Its prepared stats are empty;
    // the original catalog supplies the currency identity and pile capacity.
    for (const auto &[code, definition] : player->rules.items->entries()) {
        if (!definition.equipment.isType("gold")) continue;
        while (dropGold) {
            const auto position = ports_.items.placement({actor.area, player->position}, world.world);
            if (!position || !definition.maxStack || world.world.items.size() >= 4096) break;
            if (!ports_.items.identityCapacity(1)) return {DomainStatus::Capacity, {}};
            ItemInstance item; item.id = ports_.items.reserveIdentity(); item.definition = code;
            item.quantity = std::min(dropGold, definition.maxStack); item.location = *position;
            item.nativeSeed = uint32_t(item.id.value);
            drops.emplace_back(GroundDropFact{item});
            EquipmentValues values; values.levels.resize(player->rules.character->experience.size());
            dropGold -= item.quantity; world.goldOwners.emplace(item.id,actor.player); world.equipment.items.emplace(item.id, std::move(values)); world.world.items.emplace(item.id, std::move(item));
        }
        break;
    }
    const auto &thresholds = player->rules.character->experience;
    record.gold+=dropGold; // Preserve undropped gold when ground capacity is exhausted.
    if (!pvp && record.level > 1 && size_t(record.level + 1) < thresholds.size()) {
        const auto floor = thresholds[size_t(record.level)];
        if (record.experience < floor) return {DomainStatus::InvalidRequest, {}};
        const auto loss = std::min(record.experience - floor, (thresholds[size_t(record.level + 1)] - floor) * unsigned(player->rules.character->deathExperiencePenalty) / 100);
        record.experience -= loss; corpse.recoverableExperience = loss * 75 / 100;
    }
    if (corpse.id) corpses.push_back(corpse);
    world.equipment.includeSets(*equipment);
    transactions::InventoryEdit edit{actor, player->inventoryRevision, player->characterRevision,
        std::move(draft.edit.inventory), std::move(draft.edit.changes), record.weaponSet};
    edit.character = std::move(record); edit.corpses = std::move(corpses); edit.equipment = std::move(equipment);
    // PlrMsg::sub_6FC820C0 sends the owner's actual GOLDLOST value with
    // death mode. Capture the penalty in this transaction, including zero.
    edit.facts.emplace_back(DeathFact{transition == state_.transitions.end() ? 0 : transition->second.occurrence,
        actor.actor, {}, actor.area, uint32_t(penalty)});
    edit.publicFacts = std::move(drops);
    if(pvp) for(const auto &[id,recipient]:ports_.players.all()) if(recipient.entered)
        edit.directed.push_back({id,{PlayerMessageFact{6,player->persistent.player.name,message.other,0,0,message.characterClass}}});
    edit.world = transactions::WorldEdit{world.revision, std::move(world)};
    auto plan = ports_.transactions.prepare(std::move(edit));
    return plan ? ports_.transactions.commit(std::move(*plan.value)) : DomainResult<>{plan.status, {}};
}
}
