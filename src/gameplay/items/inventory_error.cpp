#include "gameplay/items/operations.hpp"

namespace d2x {
const char *inventoryErrorText(InventoryError error) {
    switch (error) {
    case InventoryError::RequirementsNotMet:
        return "Equipment requirements are not met.";
    case InventoryError::WrongClass:
        return "This equipment is for another class.";
    case InventoryError::UnsupportedEquipment:
        return "This equipment's rules are not implemented yet.";
    case InventoryError::Unidentified:
        return "Identify this item before equipping it.";
    case InventoryError::UnsupportedUse:
        return "This item cannot be used yet.";
    case InventoryError::None:
        return "";
    case InventoryError::UnknownDefinition:
        return "Unknown item type.";
    case InventoryError::UnknownItem:
        return "That item is no longer available.";
    case InventoryError::UnknownContainer:
        return "That container is not available.";
    case InventoryError::InvalidQuantity:
        return "Invalid item quantity.";
    case InventoryError::InvalidLocation:
        return "Cannot place an item there.";
    case InventoryError::OutOfBounds:
        return "That item does not fit there.";
    case InventoryError::Occupied:
        return "That space is occupied.";
    case InventoryError::NoSpace:
        return "Not enough room.";
    case InventoryError::RestrictedItem:
        return "That item is not allowed in this container.";
    case InventoryError::AccessDenied:
        return "You cannot access that item or container here.";
    case InventoryError::SourceChanged:
        return "That item has changed. Select it again.";
    case InventoryError::NotStackable:
        return "That item cannot be stacked.";
    case InventoryError::IncompatibleStack:
        return "Those items cannot be combined.";
    case InventoryError::StackFull:
        return "That stack is full.";
    case InventoryError::InvalidRequest:
        return "Invalid item operation.";
    case InventoryError::RevisionExhausted:
        return "That item cannot be changed further.";
    }
    return "Item operation failed.";
}
} // namespace d2x
