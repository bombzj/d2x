#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include <limits>

namespace d2x {
std::optional<RegionId> GameSessionImpl::portalTown(RegionId field) const {
    const auto level = worldContent_.levels().find(int(field));
    if (level == worldContent_.levels().end() || level->second.act < 0 || level->second.act >= 5)
        return std::nullopt;
    const auto town = RegionId(actTownLevels[size_t(level->second.act)]);
    return std::any_of(world_.regions().begin(), world_.regions().end(), [&](const auto &region) {
        return region.definition.id == town && region.definition.safe;
    }) ? std::optional<RegionId>{town} : std::nullopt;
}
void GameSessionImpl::identifyItem(const IdentifyItem &command) {
    if (auto error = previewInventory(command); error != InventoryError::None) {
        simulation_->emit(InventoryRejected{command.source.id, error});
        return;
    }
    const auto &source = *inventory_.item(command.source.id);
    const bool book = !inventory_.catalog().find(source.definition)->bookScroll.empty();
    auto consumed = book ? inventory_.consumeBookCharge(command.source, inventoryAccess())
                         : inventory_.consume(command.source, 1, inventoryAccess());
    if (!consumed) {
        publishInventory(std::move(consumed), command.source.id);
        return;
    }
    auto &target = inventory_.state_.items.at(command.target.id);
    target.identified = true;
    ++target.revision;
    consumed.item = target.id;
    consumed.changes.push_back({target.id, target.revision, ItemChangeKind::QuantityChanged,
                                target.location, target.location, target.quantity});
    publishInventory(std::move(consumed), command.source.id);
}
void GameSessionImpl::useItem(ItemHandle handle) {
    if (const auto *item = inventory_.item(handle.id)) {
        const auto *definition = inventory_.catalog().find(item->definition);
        if (definition && (content_.isPortalScroll(item->definition) || content_.isPortalScroll(definition->bookScroll))) {
            const auto level = worldContent_.levels().find(int(region().definition.id));
            if (level != worldContent_.levels().end() && level->second.act >= 0 && level->second.act < 5)
                ensureRegion(RegionId(actTownLevels[size_t(level->second.act)]));
        }
    }
    auto error = previewInventory(UseItem{handle});
    if (error != InventoryError::None) {
        simulation_->emit(InventoryRejected{handle.id, error});
        return;
    }
    auto code = inventory_.item(handle.id)->definition;
    const auto *definition = inventory_.catalog().find(code);
    if (code == content_.prisonOfIce.scroll) {
        auto result = inventory_.consume(handle, 1, inventoryAccess());
        if (!result) { publishInventory(std::move(result), handle.id); return; }
        auto &record = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::PrisonOfIce)];
        record.flags |= iceScrollUsed;
        refreshCharacter(); publishInventory(std::move(result), handle.id);
        simulation_->emit(ItemUsed{handle.id, code}); simulation_->emit(QuestAdvanced{QuestId::PrisonOfIce, record.stage}); return;
    }
    if (code == content_.goldenBird.potion) {
        auto result = inventory_.consume(handle, 1, inventoryAccess());
        if (!result) { publishInventory(std::move(result), handle.id); return; }
        auto &record = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty)).at(questIndex(QuestId::GoldenBird));
        record.flags &= ~goldenBirdPotionPending;
        refreshCharacter();
        publishInventory(std::move(result), handle.id);
        simulation_->emit(ItemUsed{handle.id, std::move(code)});
        simulation_->emit(QuestAdvanced{QuestId::GoldenBird, record.stage});
        return;
    }
    if (code == "ass") {
        auto result = inventory_.consume(handle, 1, inventoryAccess());
        if (!result) { publishInventory(std::move(result), handle.id); return; }
        auto &record = simulation_->state_.player.character.quests
            .at(size_t(state().population.difficulty)).at(questIndex(QuestId::RadamentsLair));
        record.flags = (record.flags & ~radamentBookPending) | radamentBookUsed;
        ++simulation_->state_.player.character.unspentSkills;
        publishInventory(std::move(result), handle.id);
        simulation_->emit(ItemUsed{handle.id, std::move(code)});
        simulation_->emit(QuestAdvanced{QuestId::RadamentsLair, record.stage});
        return;
    }
    if (content_.isPortalScroll(code) || content_.isPortalScroll(definition->bookScroll)) {
        auto &portal = simulation_->state_.portal;
        const auto town = portalTown(region().definition.id);
        if (!town || !townPortalArrivals_.contains(*town)) return;
        TownPortalState next{true, state().nextPortalRevision + 1, region().definition.id,
                     state().player.movement.pos, townPortalArrivals_.at(*town), state().time};
        auto result = definition->bookScroll.empty()
                          ? inventory_.consume(handle, 1, inventoryAccess())
                          : inventory_.consumeBookCharge(handle, inventoryAccess());
        if (result) {
            if (int(region().definition.id) == 120) resetAncients();
            cancelExit();
            cancelPickup();
            cancelInteraction();
            portal = next;
            simulation_->state_.nextPortalRevision = next.revision;
        }
        bool consumed = bool(result);
        publishInventory(std::move(result), handle.id);
        if (consumed)
            simulation_->emit(ItemUsed{handle.id, std::move(code)});
        return;
    }
    auto potion = *content_.potion(code);
    auto result = inventory_.drink(handle, inventoryAccess());
    bool consumed = bool(result);
    publishInventory(std::move(result), handle.id);
    if (consumed) {
        simulation_->applyPotion(potion);
        simulation_->emit(ItemUsed{handle.id, std::move(code)});
    }
}
InventoryError GameSessionImpl::previewPortalScroll(ItemHandle handle) const {
    if (auto error = inventory_.checkHandle(handle); error != InventoryError::None)
        return error;
    const auto *item = inventory_.item(handle.id);
    auto location = std::get_if<ContainerLocation>(&item->location);
    const auto &player = state().player;
    if (!location || location->container != playerContainers_.backpack ||
        (!inventory_.catalog().find(item->definition)->bookScroll.empty() && !item->charges) ||
        player.actions.dead ||
        player.resources.hp <= 0 || region().definition.safe ||
        player.actions.castTime > 0 || player.actions.meleeTime > 0)
        return InventoryError::AccessDenied;
    if (!portalTown(region().definition.id) || !portalResources_ || portalReach_ <= 0 ||
        !map().grid.walkable(player.movement.pos))
        return InventoryError::UnsupportedUse;
    const auto town = *portalTown(region().definition.id);
    for (const auto &region : world_.regions())
        if (region.definition.id == town && region.loaded && !townPortalArrivals_.contains(town))
            return InventoryError::UnsupportedUse;
    if (state().nextPortalRevision == std::numeric_limits<uint64_t>::max())
        return InventoryError::RevisionExhausted;
    return InventoryError::None;
}
std::optional<Vec> GameSessionImpl::portalPosition() const {
    const auto &portal = state().portal;
    if (!portal.active)
        return std::nullopt;
    if (portalTown(portal.field) == region().definition.id)
        return portal.townPosition;
    if (region().definition.id == portal.field)
        return portal.fieldPosition;
    return std::nullopt;
}
const TownPortalState *GameSessionImpl::findPortal(uint64_t revision) const {
    if (state().portal.active && state().portal.revision == revision) return &state().portal;
    for (const auto &portal : state().publicPortals)
        if (portal.active && portal.revision == revision) return &portal;
    return nullptr;
}
std::vector<GameSessionImpl::PortalView> GameSessionImpl::portals(RegionId region) const {
    std::vector<PortalView> result;
    auto append = [&](const TownPortalState &portal) {
        if (!portal.active) return;
        if (portalTown(portal.field) == region)
            result.push_back({portal.revision, portal.townPosition, portal.openedAt});
        else if (region == portal.field)
            result.push_back({portal.revision, portal.fieldPosition, portal.openedAt});
    };
    append(state().portal);
    for (const auto &portal : state().publicPortals) append(portal);
    return result;
}
void GameSessionImpl::beginPortal(uint64_t revision) {
    const auto *portal = findPortal(revision);
    if (!portal || state().player.actions.dead) return;
    const bool town = portalTown(portal->field) == region().definition.id;
    if (!town && region().definition.id != portal->field) return;
    const Vec position = town ? portal->townPosition : portal->fieldPosition;
    cancelExit();
    cancelPickup();
    cancelInteraction();
    closeStorage();
    pendingPortal_ = revision;
    if ((state().player.movement.pos - position).length() > portalReach_)
        simulation_->execute(MoveTo{position});
}
void GameSessionImpl::updatePortal() {
    if (!pendingPortal_) return;
    const auto *portal = findPortal(*pendingPortal_);
    const auto &player = state().player;
    const bool returning = portal && portalTown(portal->field) == region().definition.id;
    if (!portal || player.actions.dead || (!returning && region().definition.id != portal->field)) {
        cancelInteraction();
        return;
    }
    if (player.actions.castTime > 0 || player.actions.meleeTime > 0) return;
    const Vec position = returning ? portal->townPosition : portal->fieldPosition;
    if ((player.movement.pos - position).length() <= portalReach_ && map().grid.segment(player.movement.pos, position)) {
        const auto town = portalTown(portal->field);
        if (!town) { cancelInteraction(); return; }
        const auto destination = returning ? portal->field : *town;
        const Vec arrival = returning ? portal->fieldPosition : portal->townPosition;
        if (returning && portal->consumedOnReturn)
            simulation_->state_.portal.active = false;
        enter(destination, arrival);
    } else if (player.movement.route.empty()) {
        cancelInteraction();
        simulation_->emit(InteractionFailed{{}, "Cannot reach the town portal."});
    }
}
void GameSessionImpl::useBeltColumn(int column, bool hireling) {
    auto belt = inventory_.container(playerContainers_.belt);
    if (!belt || column < 0 || column >= belt->spec.columns) {
        simulation_->emit(InventoryRejected{{}, InventoryError::InvalidRequest});
        return;
    }
    // A manually rearranged column may have a hole; use its lowest occupied cell.
    for (int row = 0; row < belt->spec.rows; ++row)
        if (auto item = inventory_.item(inventory_.itemAt(belt->id, {column, row}))) {
            if (hireling) useHirelingPotion(item->handle());
            else useItem(item->handle());
            return;
        }
    simulation_->emit(PickupFailed{{}, "That belt column is empty."});
}
} // namespace d2x
