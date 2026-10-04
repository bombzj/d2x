#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
void GameSessionImpl::spawnLoot(std::span<const LootDrop> drops, RegionId id, Vec origin) {
    auto region = std::find_if(world_.regions().begin(), world_.regions().end(),
                               [id](const Region &r) { return r.definition.id == id; });
    if (region == world_.regions().end())
        throw std::logic_error("Loot references an unknown region");
    const auto &grid = region->map.grid;
    for (const auto &drop : drops) {
        Vec position = origin + drop.offset;
        // Items.cpp starts at the drop offset if that room exists. The common
        // item resolver checks drop collision and the field from the actual source.
        if (position.x < 0 || position.y < 0 || position.x >= grid.width || position.y >= grid.height)
            position = origin;
        const bool questUnique = content_.staffRecipe.isComponent(drop.code);
        const auto generation = questUnique ? questItemGeneration(drop.code, inventory_.state_.creationRandom) : drop.generation;
        auto result = inventory_.createItem(drop.code, drop.quantity, GroundLocation{id, position},
                            drop.level, generation, origin);
        if (result.error == InventoryError::NoSpace) {
            simulation_->emit(LootDeferred{{}, "No free ground cell for this drop."});
            continue;
        }
        if (!result)
            throw std::logic_error("Invalid loot definition or placement");
        auto &item = inventory_.state_.items.at(result.item);
        if (questUnique) item.identified = true;
        const auto *definition = content_.items.find(item.definition);
        if (definition && content_.tables.at(definition->base.sourceTable)
                .number(definition->base.sourceRow, "questdiffcheck").value_or(0))
            item.nativeQuestDifficulty = unsigned(state().population.difficulty);
        publishInventory(std::move(result), {});
    }
}
void GameSessionImpl::cancelPickup() {
    if (pickup_.id)
        simulation_->stopWalking();
    pickup_ = {};
    pickupToCursor_ = false;
}
void GameSessionImpl::beginPickup(ItemHandle handle, bool toCursor) {
    if (pickup_.id == handle.id && pickup_.revision == handle.revision && pickupToCursor_ == toCursor)
        return;
    cancelPickup();
    const auto *item = inventory_.item(handle.id);
    if (!item || item->revision != handle.revision) {
        simulation_->emit(
            InventoryRejected{handle.id, item ? InventoryError::SourceChanged : InventoryError::UnknownItem});
        return;
    }
    const auto *ground = std::get_if<GroundLocation>(&item->location);
    const auto &player = state().player;
    if (player.actions.dead || cursorItem() || !ground || ground->region != region().definition.id) {
        simulation_->emit(InventoryRejected{handle.id, InventoryError::AccessDenied});
        return;
    }
    simulation_->stopWalking();
    simulation_->execute(MoveTo{ground->position});
    if (player.movement.route.empty() && (ground->position - player.movement.pos).length() > 1.8f) {
        simulation_->emit(PickupFailed{handle.id, "Cannot reach that item."});
        return;
    }
    pickup_ = handle;
    pickupToCursor_ = toCursor;
}
void GameSessionImpl::updatePickup() {
    if (!pickup_.id)
        return;
    const auto &player = state().player;
    if (player.actions.dead || cursorItem()) {
        cancelPickup();
        return;
    }
    const auto handle = pickup_;
    const auto *item = inventory_.item(handle.id);
    if (!item || item->revision != handle.revision) {
        cancelPickup();
        simulation_->emit(
            InventoryRejected{handle.id, item ? InventoryError::SourceChanged : InventoryError::UnknownItem});
        return;
    }
    const auto *ground = std::get_if<GroundLocation>(&item->location);
    if (!ground || ground->region != region().definition.id) {
        cancelPickup();
        simulation_->emit(InventoryRejected{handle.id, InventoryError::AccessDenied});
        return;
    }
    const auto *base = content_.items.find(item->definition);
    const auto &table = content_.tables.at(base->base.sourceTable);
    const bool questItem = table.number(base->base.sourceRow, "quest").value_or(0) != 0;
    if (questItem && ((table.number(base->base.sourceRow, "questdiffcheck").value_or(0) &&
        item->nativeQuestDifficulty < unsigned(state().population.difficulty)) ||
        (item->definition == "ass" && !(quest(QuestId::RadamentsLair).flags & radamentBookPending)) ||
        (table.number(base->base.sourceRow, "quest") == 10 && item->definition != content_.cubeCode &&
         quest(QuestId::HoradricStaff).stage >= 6) || carriesQuestItem(item->definition))) {
        cancelPickup();
        simulation_->emit(InventoryRejected{handle.id, InventoryError::RestrictedItem});
        return;
    }
    if (player.actions.castTime > 0 || player.actions.meleeTime > 0)
        return;
    auto access = inventoryAccess();
    // Pickup is deliberately closer than generic container access; walls also block the hand-off.
    access.reach = 1.8f;
    if ((ground->position - player.movement.pos).length() <= access.reach &&
        map().grid.collisionSegment(player.movement.pos, ground->position, 0x0801)) {
        auto definition = item->definition;
        unsigned quantity = item->quantity;
        if (inventory_.catalog().find(definition)->equipment.isType("gold")) {
            unsigned capacity = unsigned(equipmentActor().level) * 10000;
            unsigned amount = std::min(quantity, capacity - player.character.gold);
            if (!amount) {
                cancelPickup();
                simulation_->emit(PickupFailed{handle.id, "Gold carrying limit reached."});
                return;
            }
            auto result = inventory_.consume(handle, amount, access);
            if (result)
                simulation_->state_.player.character.gold += amount;
            bool collected = bool(result);
            cancelPickup();
            publishInventory(std::move(result), handle.id);
            if (collected)
                simulation_->emit(ItemPickedUp{handle.id, std::move(definition), amount});
            return;
        }
        auto result = pickupToCursor_
            ? inventory_.move(MoveItem{handle, ContainerLocation{playerContainers_.cursor, {}}}, access)
            : inventory_.collect(handle, playerContainers_, access);
        quantity = result.transferred;
        const bool remainder = inventory_.item(handle.id) &&
            std::holds_alternative<GroundLocation>(inventory_.item(handle.id)->location);
        bool collected = bool(result);
        cancelPickup();
        publishInventory(std::move(result), handle.id);
        if (collected)
            simulation_->emit(ItemPickedUp{handle.id, std::move(definition), quantity});
        if (collected && remainder)
            simulation_->emit(PickupFailed{handle.id, "Merged what fits. The remainder stays on the ground."});
    } else if (player.movement.route.empty()) {
        cancelPickup();
        simulation_->emit(PickupFailed{handle.id, "Cannot reach that item."});
    }
}
} // namespace d2x
