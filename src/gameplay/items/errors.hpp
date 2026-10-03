#pragma once

namespace d2x {
enum class InventoryError {
    None,
    UnknownDefinition,
    UnknownItem,
    UnknownContainer,
    InvalidQuantity,
    InvalidLocation,
    OutOfBounds,
    Occupied,
    NoSpace,
    RestrictedItem,
    AccessDenied,
    SourceChanged,
    NotStackable,
    IncompatibleStack,
    StackFull,
    InvalidRequest,
    RevisionExhausted,
    UnsupportedUse,
    RequirementsNotMet,
    WrongClass,
    UnsupportedEquipment,
    Unidentified
};
const char *inventoryErrorText(InventoryError error);
} // namespace d2x
