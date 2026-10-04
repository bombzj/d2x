#pragma once
#include "gameplay/items/intents.hpp"
#include "gameplay/items/display.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace d2x {
struct InventoryDefinitionView {
    bool socketFiller = false;
    std::string code, name, bookScroll;
    int width = 0, height = 0, beltRows = 0;
    unsigned maxStack = 0, bookCapacity = 0;
    bool beltAllowed = false, opensCube = false, identifySource = false;
    std::array<bool, size_t(EquipmentSlot::Count)> slots{};
    bool fits(EquipmentSlot slot) const { return slots.at(size_t(slot)); }
};
struct InventoryItemView {
    EntityId id;
    uint64_t revision = 0;
    std::string definition, artKey, name;
    ItemLocation location;
    ItemQuality quality = ItemQuality::Normal;
    bool identified = true;
    unsigned quantity = 0, durability = 0, charges = 0;
    std::vector<ItemTextLine> tooltip;
    ItemHandle handle() const { return {id, revision}; }
};
struct InventoryContainerView {
    EntityId id;
    ContainerKind kind = ContainerKind::Backpack;
    int columns = 0, rows = 0;
};
struct InventoryLayoutView {
    int columns = 0, rows = 0, left = 0, top = 0, cellSize = 0;
    bool expansion = false;
};
// A client-owned projection of this player's inventory and the open storage.
// It contains no authority references, rolled native properties or random state.
struct InventoryView {
    uint64_t revision = 0;
    PlayerContainers containers;
    EntityId storage;
    unsigned weaponSet = 0, gold = 0, bankGold = 0;
    unsigned bankGoldLimit = 0, groundGoldLimit = 0, walletLimit = 0;
    bool dead = false;
    InventoryLayoutView stashLayout, cubeLayout;
    std::array<std::array<int, 4>, 4> hirelingSlots{};
    std::string cubeCode, staffRecipeOutput;
    std::optional<GroundLocation> dropLocation;
    std::map<EntityId, InventoryContainerView> containerViews;
    std::map<EntityId, InventoryItemView> items;
    std::map<std::string, InventoryDefinitionView, std::less<>> definitions;
    const InventoryItemView *item(EntityId id) const;
    const InventoryContainerView *container(EntityId id) const;
    const InventoryDefinitionView *definition(std::string_view code) const;
    EntityId itemAt(EntityId container, Cell cell) const;
    std::vector<EntityId> contents(EntityId container) const;
    EntityId equipped(const PlayerContainers &owned, EquipmentSlot slot) const;
    const InventoryItemView *cursorItem() const;
};
} // namespace d2x
