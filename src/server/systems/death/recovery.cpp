#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/progression/system.hpp"
#include "server/systems/social/system.hpp"
#include "gameplay/items/inventory_order.hpp"
#include "gameplay/combat/geometry.hpp"
#include <algorithm>
#include <array>
namespace d2x::server::death {
const PlayerState *System::corpseOwner(const ActorContext &actor, EntityId target) const {
    for (const auto &[id, owner] : ports_.players.all()) {
        if (!owner.entered) continue;
        const auto transition = state_.transitions.find(owner.actor);
        if (id != actor.player && (!owner.persistent.player.hardcoreDead ||
            !(ports_.social.flags(id, actor.player) & 1) || transition == state_.transitions.end() ||
            !transition->second.companionsSettled || actor.tick < transition->second.ready)) continue;
        if (std::any_of(owner.persistent.corpses.begin(), owner.persistent.corpses.end(),
            [&](const auto &corpse) { return corpse.id == target && corpse.owner == owner.actor && corpse.region == actor.area; })) return &owner;
    }
    return nullptr;
}
DomainResult<> System::recover(const ActorContext &actor, EntityId target) {
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || !area || area->generation != actor.areaGeneration || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    const auto *owner = corpseOwner(actor, target);
    if (!owner) return {DomainStatus::InvalidRequest, {}};
    const bool foreign = owner != player;
    if (foreign && (!player->persistent.player.hardcore || owner->area != actor.area)) return {DomainStatus::InvalidRequest, {}};
    auto corpses = owner->persistent.corpses;
    auto found = std::find_if(corpses.begin(), corpses.end(), [&](const auto &corpse) { return corpse.id == target; });
    if (found == corpses.end() || found->region != actor.area ||
        meleeDistance(player->position, 2, found->position, 2) > 8 || !ports_.areas.at(actor.area).definition.collision.collisionSegment(player->position, found->position, 0x0801)) return {DomainStatus::InvalidRequest, {}};
    auto equipment = std::make_shared<EquipmentRules>(*player->rules.equipment);
    auto input = *player;
    if (foreign) {
        auto storage = owner->persistent.inventory.containers.at(found->items);
        storage.spec.owner = actor.actor;
        input.persistent.inventory.containers.emplace(storage.id, storage);
        for (const auto id : inventoryItemOrder(owner->persistent.inventory)) {
            const auto &item = owner->persistent.inventory.items.at(id);
            const auto *at = std::get_if<ContainerLocation>(&item.location);
            if (!at || at->container != found->items) continue;
            input.persistent.inventory.items.emplace(id, item);
            input.persistent.inventory.itemOrder.push_back(id);
            equipment->items.emplace(id, owner->rules.equipment->items.at(id));
        }
        equipment->includeSets(*owner->rules.equipment);
        input.rules.equipment = equipment;
    }
    inventory::detail::Draft draft(input, *player->rules.items, *equipment, *player->rules.character);
    const ContainerLocation cursor{draft.containers().cursor, {}};
    if (draft.at(cursor)) return {DomainStatus::Conflict, {}};
    const auto storage = found->items;
    const auto carryAllowed = [&](EntityId id) {
        const auto &item = draft.edit.inventory.items.at(id);
        const auto *definition = player->rules.items->find(item.definition);
        const bool cube = definition && definition->opensCube;
        if (!cube && !equipment->items.at(id).singleCarry) return true;
        for (const auto &[otherId, other] : draft.edit.inventory.items) {
            if (otherId == id || (cube ? other.definition != item.definition : other.quality != ItemQuality::Unique || other.specialRow != item.specialRow)) continue;
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
                inventory::detail::Draft trial(input, *player->rules.items, *equipment, *player->rules.character);
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
        const auto original = draft.edit.inventory.items.at(id).location;
        draft.edit.inventory.items.at(id).location = cursor;
        const auto equipped = draft.autoEquip(id);
        if (!equipped) return {equipped.status,{}};
        if (*equipped.value) {
            for (auto &change:draft.edit.changes) if (change.item==id) change.before=original;
            continue;
        }
        draft.edit.inventory.items.at(id).location=original;
        const auto &item = draft.edit.inventory.items.at(id);
        const auto *definition = player->rules.items->find(item.definition);
        std::optional<ContainerLocation> destination;
        if (definition && definition->beltAllowed) destination = draft.space(id, draft.containers().belt);
        if (!destination) destination = draft.space(id, draft.containers().backpack);
        if (destination) {
            const auto result = draft.move(id, *destination);
            if (result != DomainStatus::Applied) return {result, {}};
        }
    }
    auto record = player->persistent.player;
    const auto experience = foreign ? 0 : found->recoverableExperience;
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
    if (foreign) {
        auto source = owner->persistent.inventory;
        std::vector<ItemChange> removed;
        for (auto &change : draft.edit.changes) {
            const auto original = source.items.find(change.item);
            if (original == source.items.end()) {
                if (player->persistent.inventory.items.contains(change.item)) continue;
                return {DomainStatus::InvalidRequest, {}};
            }
            removed.push_back({change.item, change.revision, ItemChangeKind::Removed, original->second.location, {}, 0});
            source.items.erase(original);
            change.kind = ItemChangeKind::Created; change.before = {};
        }
        if (removed.empty()) return {DomainStatus::Conflict, {}};
        for (auto item = draft.edit.inventory.items.begin(); item != draft.edit.inventory.items.end();) {
            const auto *at = std::get_if<ContainerLocation>(&item->second.location);
            if (at && at->container == storage) item = draft.edit.inventory.items.erase(item);
            else ++item;
        }
        draft.edit.inventory.containers.erase(storage);
        if (empty) source.containers.erase(storage);
        const ActorContext sourceActor{owner->player, owner->actor, owner->area, actor.areaGeneration, 0, actor.tick};
        transactions::InventoryEdit take{actor, player->inventoryRevision, player->characterRevision,
            std::move(draft.edit.inventory), std::move(draft.edit.changes), record.weaponSet};
        take.equipment = std::move(equipment); take.lootedCorpse = target;
        transactions::InventoryEdit give{sourceActor, owner->inventoryRevision, owner->characterRevision,
            std::move(source), std::move(removed), owner->persistent.player.weaponSet};
        give.corpses = std::move(corpses); give.lootedCorpse = target;
        std::vector<transactions::Change> changes; changes.emplace_back(std::move(take)); changes.emplace_back(std::move(give));
        auto plans = ports_.transactions.prepareGroup(std::move(changes));
        if (!plans) return {plans.status,{}};
        auto committed=ports_.transactions.commitInventories(std::move(*plans.value));
        return committed;
    }
    transactions::InventoryEdit edit{actor, player->inventoryRevision, player->characterRevision,
        std::move(draft.edit.inventory), std::move(draft.edit.changes), record.weaponSet};
    edit.resources = record.level > player->persistent.player.level ? transactions::ResourceRefresh::LevelUp : transactions::ResourceRefresh::Clamp;
    edit.character = std::move(record); edit.corpses = std::move(corpses); edit.equipment = std::move(equipment);
    auto plan = ports_.transactions.prepare(std::move(edit));
    return plan ? ports_.transactions.commit(std::move(*plan.value)) : DomainResult<>{plan.status, {}};
}
}
