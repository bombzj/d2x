#pragma once
#include "gameplay/items/intents.hpp"
#include <vector>

namespace d2x::server::inventory {
// Inventory access and placement plans; no second occupancy or item copy.
struct GroundTransfer { ItemHandle item; bool drop{}, cursor{}; std::optional<ItemHandle> cube{}; };
// Native direct placement: cursor/ground to an owned cube, or pack to belt.
struct AutoStoreItem { ItemHandle item; std::optional<ItemHandle> cube{}; };
struct CloseCube {};
using Intent = std::variant<GroundTransfer, AutoStoreItem, MoveItem, TransferItem, SwapItems, SplitStack, MergeStacks,
    LoadBook, IdentifyItem, EquipBelt, EquipItem, EquipHirelingItem, UseItem,
    UseBeltColumn, UseHirelingPotion, SwitchWeaponSet, CloseStorage, CloseCube, GoldTransaction>;
enum class Source { Stored, Cursor, Belt };
enum class EquipmentMode { Insert, Indirect, Swap, TwoHanded };
struct Request {
    Intent intent;
    Source source = Source::Cursor;
    EquipmentMode equipmentMode = EquipmentMode::Insert;
    std::optional<unsigned> weaponSet{};
    std::vector<ItemHandle> equipmentGuards{};
};
}
