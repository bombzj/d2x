#include "inventory.hpp"

namespace d2x {
namespace {
InventoryResult rejected(InventoryError error) {
    InventoryResult result;
    result.error = error;
    return result;
}
}
InventoryError InventoryService::preview(const LoadBook &command, const InventoryAccess &access) const {
    if (command.scroll.id == command.book.id) return InventoryError::InvalidRequest;
    if (auto error = checkHandle(command.scroll); error != InventoryError::None) return error;
    if (auto error = checkHandle(command.book); error != InventoryError::None) return error;
    const auto &scroll = state_.items.at(command.scroll.id);
    const auto &book = state_.items.at(command.book.id);
    const auto *definition = catalog_.find(book.definition);
    if (!definition || definition->bookScroll != scroll.definition ||
        scroll.quality != ItemQuality::Normal || book.quality != ItemQuality::Normal ||
        scroll.quantity != 1 || book.quantity != 1)
        return InventoryError::IncompatibleStack;
    if (book.charges >= definition->bookCapacity) return InventoryError::StackFull;
    if (auto error = checkAccess(scroll.location, access); error != InventoryError::None) return error;
    return checkAccess(book.location, access);
}
InventoryResult InventoryService::loadBook(const LoadBook &command, const InventoryAccess &access) {
    if (auto error = preview(command, access); error != InventoryError::None) return rejected(error);
    auto &book = state_.items.at(command.book.id);
    const auto &scroll = state_.items.at(command.scroll.id);
    InventoryResult result;
    result.item = book.id;
    result.transferred = 1;
    result.changes.reserve(2);
    result.changes.push_back({book.id, book.revision + 1, ItemChangeKind::QuantityChanged,
                              book.location, book.location, book.quantity});
    result.changes.push_back({scroll.id, scroll.revision + 1, ItemChangeKind::Removed,
                              scroll.location, std::nullopt, 0});
    ++book.charges;
    ++book.revision;
    state_.items.erase(scroll.id);
    return result;
}
InventoryResult InventoryService::consumeBookCharge(ItemHandle handle, const InventoryAccess &access) {
    if (auto error = checkHandle(handle); error != InventoryError::None) return rejected(error);
    auto &book = state_.items.at(handle.id);
    const auto *definition = catalog_.find(book.definition);
    if (!definition || definition->bookScroll.empty() || !book.charges)
        return rejected(InventoryError::InvalidQuantity);
    if (auto error = checkAccess(book.location, access); error != InventoryError::None) return rejected(error);
    InventoryResult result;
    result.item = book.id;
    result.transferred = 1;
    result.changes.push_back({book.id, book.revision + 1, ItemChangeKind::QuantityChanged,
                              book.location, book.location, book.quantity});
    --book.charges;
    ++book.revision;
    return result;
}
} // namespace d2x
