#include "inventory.hpp"
#include <iterator>
#include <utility>

namespace d2x {
InventoryResult InventoryService::replaceItem(ItemHandle source, std::string_view definition,
        const ItemDestination &destination, const InventoryAccess &access, unsigned level,
        const ItemGeneration &generation, std::optional<uint64_t> preparedRandom) {
    auto fail = [](InventoryError error) { InventoryResult result; result.error = error; return result; };
    if (const auto error = checkHandle(source); error != InventoryError::None) return fail(error);
    if (state_.items.at(source.id).quantity != 1) return fail(InventoryError::InvalidQuantity);
    if (const auto error = checkDestinationAccess(destination, access); error != InventoryError::None) return fail(error);
    auto draftIds = ids_;
    InventoryService draft{draftIds, catalog_, stashDimensions_, cubeDimensions_};
    draft.state_ = state_;
    draft.itemProperties_ = itemProperties_;
    draft.groundPlacement_ = groundPlacement_;
    draft.singleCarryUniques_ = singleCarryUniques_;
    auto removed = draft.consume(source, 1, access);
    if (!removed) return removed;
    if (preparedRandom) draft.state_.creationRandom = *preparedRandom;
    auto created = draft.createItem(definition, 1, destination, level, generation);
    if (!created) return created;
    // Notification allocation also completes before any authoritative mutation.
    removed.changes.reserve(removed.changes.size() + created.changes.size());
    removed.changes.insert(removed.changes.end(), std::make_move_iterator(created.changes.begin()),
                            std::make_move_iterator(created.changes.end()));
    removed.item = created.item;
    using std::swap;
    swap(state_, draft.state_);
    ids_ = draftIds;
    return removed;
}
} // namespace d2x
