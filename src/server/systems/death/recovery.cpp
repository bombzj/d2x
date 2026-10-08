#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/progression/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include <algorithm>
#include <array>
namespace d2x::server::death {
DomainResult<> System::recover(const ActorContext &actor, EntityId target) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || player->actor != actor.actor || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    auto corpses = player->persistent.corpses;
    auto found = std::find_if(corpses.begin(), corpses.end(), [&](const auto &corpse) { return corpse.id == target; });
    if (found == corpses.end() || found->owner != actor.actor || found->region != actor.area ||
        meleeDistance(player->position, 2, found->position, 2) > 8 || !ports_.areas.at(actor.area).definition.collision.collisionSegment(player->position, found->position, 0x0801)) return {DomainStatus::InvalidRequest, {}};
    inventory::detail::Draft draft(*player, *player->rules.items, *player->rules.equipment, *player->rules.character);
    const ContainerLocation cursor{draft.containers().cursor, {}};
    if (draft.at(cursor)) return {DomainStatus::Conflict, {}};
    const auto storage = found->items;
    const auto carryAllowed = [&](EntityId id) {
        const auto &item = draft.edit.inventory.items.at(id);
        if (!player->rules.equipment->items.at(id).singleCarry) return true;
        for (const auto &[otherId, other] : draft.edit.inventory.items) {
            if (otherId == id || other.quality != ItemQuality::Unique || other.specialRow != item.specialRow) continue;
            const auto *at = std::get_if<ContainerLocation>(&other.location);
            if (at && draft.edit.inventory.containers.at(at->container).spec.kind != ContainerKind::Corpse) return false;
        }
        return true;
    };
    bool progress;
    do {
        progress = false;
        for (int index = 0; index < int(EquipmentSlot::Count); ++index) {
            const auto id = draft.at({storage, {index, 0}});
            if (!id || !carryAllowed(id)) continue;
            const auto slot = EquipmentSlot(index);
            std::array<EquipmentSlot, 2> choices{slot, slot};
            switch (slot) {
            case EquipmentSlot::RightRing: choices[1] = EquipmentSlot::LeftRing; break;
            case EquipmentSlot::LeftRing: choices[1] = EquipmentSlot::RightRing; break;
            case EquipmentSlot::RightHand: choices[1] = EquipmentSlot::LeftHand; break;
            case EquipmentSlot::LeftHand: choices[1] = EquipmentSlot::RightHand; break;
            case EquipmentSlot::AlternateRightHand: choices[1] = EquipmentSlot::AlternateLeftHand; break;
            case EquipmentSlot::AlternateLeftHand: choices[1] = EquipmentSlot::AlternateRightHand; break;
            default: break;
            }
            for (const auto choice : choices) {
                if (draft.equipped(choice)) continue;
                inventory::detail::Draft trial(*player, *player->rules.items, *player->rules.equipment, *player->rules.character);
                trial.edit = draft.edit; trial.edit.changes.clear();
                auto &item = trial.edit.inventory.items.at(id); const auto original = item.location;
                item.location = cursor;
                if (choice == EquipmentSlot::AlternateRightHand || choice == EquipmentSlot::AlternateLeftHand) trial.edit.weaponSet = 1;
                else if (choice == EquipmentSlot::RightHand || choice == EquipmentSlot::LeftHand) trial.edit.weaponSet = 0;
                const auto result = trial.equipment({item.handle(), choice, {}}, inventory::EquipmentMode::Insert);
                if (result != DomainStatus::Applied || trial.edit.changes.size() != 1 || trial.edit.changes.front().item != id) continue;
                trial.edit.changes.front().before = original;
                auto change = trial.edit.changes.front();
                draft.edit.inventory = std::move(trial.edit.inventory); draft.edit.changes.push_back(std::move(change));
                progress = true; break;
            }
        }
    } while (progress);
    std::vector<EntityId> remaining;
    for (const auto &[id, item] : draft.edit.inventory.items)
        if (const auto *at = std::get_if<ContainerLocation>(&item.location); at && at->container == storage) remaining.push_back(id);
    for (const auto id : remaining) {
        if (!carryAllowed(id)) continue;
        const auto &item = draft.edit.inventory.items.at(id);
        const auto *definition = player->rules.items->find(item.definition);
        std::optional<ContainerLocation> destination;
        if (definition && definition->autoBelt) destination = draft.space(id, draft.containers().belt);
        if (!destination) destination = draft.space(id, draft.containers().backpack);
        if (destination) {
            const auto result = draft.move(id, *destination);
            if (result != DomainStatus::Applied) return {result, {}};
        }
    }
    auto record = player->persistent.player;
    const auto experience = found->recoverableExperience;
    if (experience) {
        auto reward = progression::addExperience(record, player->definition, *player->rules.character, experience);
        if (reward) record = std::move(*reward.value);
        else if (reward.status != DomainStatus::Conflict) return {reward.status, {}};
        found->recoverableExperience = 0;
    }
    const bool empty = std::none_of(draft.edit.inventory.items.begin(), draft.edit.inventory.items.end(), [&](const auto &entry) {
        const auto *at = std::get_if<ContainerLocation>(&entry.second.location); return at && at->container == storage;
    });
    if (empty) { corpses.erase(found); draft.edit.inventory.containers.erase(storage); }
    else if (draft.edit.changes.empty() && !experience) return {DomainStatus::Conflict, {}};
    // Keep the old corpse container in the immutable delta, since its source
    // locations still need encoding after the authoritative container is removed.
    transactions::InventoryEdit edit{actor, player->inventoryRevision, player->characterRevision,
        std::move(draft.edit.inventory), std::move(draft.edit.changes), record.weaponSet};
    edit.resources = record.level > player->persistent.player.level ? transactions::ResourceRefresh::LevelUp : transactions::ResourceRefresh::Clamp;
    edit.character = std::move(record); edit.corpses = std::move(corpses);
    auto plan = ports_.transactions.prepare(std::move(edit));
    return plan ? ports_.transactions.commit(std::move(*plan.value)) : DomainResult<>{plan.status, {}};
}
}
