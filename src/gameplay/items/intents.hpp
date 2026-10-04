#pragma once
#include "gameplay/items/operations.hpp"
#include <variant>

namespace d2x {
struct SwitchWeaponSet {};
struct EquipHirelingItem {
    ItemHandle item;
    std::optional<EquipmentSlot> slot;
    std::optional<ItemDestination> destination;
};
struct UseHirelingPotion { ItemHandle item; };
struct TransmuteCube {};
struct CloseStorage {};
enum class GoldAction { Deposit, Withdraw, Drop };
struct GoldTransaction {
    GoldAction action = GoldAction::Drop;
    unsigned amount = 0;
};
using InventoryIntent = std::variant<MoveItem, TransferItem, SwapItems, SplitStack, MergeStacks,
    LoadBook, SocketItem, IdentifyItem, EquipBelt, EquipItem, EquipHirelingItem, UseItem, UseBeltColumn,
    UseHirelingPotion, SwitchWeaponSet, TransmuteCube, CloseStorage, GoldTransaction>;
} // namespace d2x
