#pragma once
#include "definitions.hpp"
#include "operations.hpp"

namespace d2x {
class InventoryService {
    friend class GameSession;
    EntityIds &ids_;
    ItemCatalog catalog_;
    InventoryState state_;
    void validateSnapshot(const InventoryState &state, const PlayerContainers &containers,
                          EntityId player) const;
    InventoryError checkHandle(ItemHandle handle) const;
    InventoryError checkAccess(const ItemLocation &location, const InventoryAccess &access) const;
    InventoryError checkDestinationAccess(const ItemDestination &destination,
                                          const InventoryAccess &access) const;
    InventoryError checkPlacement(const ItemDefinition &definition, const ItemLocation &location,
                                  EntityId ignore = {}, EntityId alsoIgnore = {}) const;
    InventoryError resolve(const ItemDefinition &definition, const ItemDestination &destination,
                           ItemLocation &location, EntityId ignore = {}) const;
    bool overlaps(const ItemDefinition &a, const ItemLocation &aPosition, const ItemDefinition &b,
                  const ItemLocation &bPosition) const;
    InventoryResult planTransfer(const TransferItem &command, const InventoryAccess &access) const;
    InventoryResult planBelt(const EquipBelt &command, const PlayerContainers &containers,
                             const InventoryAccess &access, InventoryState *replacement) const;

  public:
    InventoryService(EntityIds &ids, ItemCatalog catalog) : ids_(ids), catalog_(std::move(catalog)) {}
    InventoryService(const InventoryService &) = delete;
    InventoryService &operator=(const InventoryService &) = delete;
    const InventoryState &state() const { return state_; }
    const ItemCatalog &catalog() const { return catalog_; }
    const ItemInstance *item(EntityId id) const;
    const ContainerState *container(EntityId id) const;
    EntityId itemAt(EntityId container, Cell cell) const;
    std::vector<EntityId> contents(EntityId container) const;
    std::vector<EntityId> groundItems(RegionId region) const;
    std::optional<Cell> findSpace(EntityId container, std::string_view definition,
                                  EntityId ignore = {}) const;
    // The UI and committing operations share these read-only rules.
    InventoryError preview(const TransferItem &command, const InventoryAccess &access) const;
    InventoryResult transfer(const TransferItem &command, const InventoryAccess &access);
    InventoryError preview(const MoveItem &command, const InventoryAccess &access) const;
    InventoryError preview(const SwapItems &command, const InventoryAccess &access) const;
    InventoryError preview(const SplitStack &command, const InventoryAccess &access) const;
    InventoryError preview(const MergeStacks &command, const InventoryAccess &access) const;
    InventoryError preview(const EquipBelt &command, const PlayerContainers &containers,
                           const InventoryAccess &access) const;
    InventoryResult equipBelt(const EquipBelt &command, const PlayerContainers &containers,
                              const InventoryAccess &access);
    std::optional<Cell> beltSpace(EntityId belt, std::string_view code, bool automaticPickup) const;
    InventoryError previewDrink(ItemHandle item, const InventoryAccess &access) const;
    InventoryResult drink(ItemHandle item, const InventoryAccess &access);

    // Trusted gameplay creation APIs. UI commands cannot mint items or containers.
    EntityId createContainer(ContainerSpec specification);
    PlayerContainers createPlayerContainers(EntityId player);
    InventoryResult createItem(std::string_view definition, unsigned quantity,
                               const ItemDestination &destination);
    InventoryResult move(const MoveItem &command, const InventoryAccess &access);
    InventoryResult swap(const SwapItems &command, const InventoryAccess &access);
    InventoryResult split(const SplitStack &command, const InventoryAccess &access);
    InventoryResult merge(const MergeStacks &command, const InventoryAccess &access);
    InventoryResult consume(ItemHandle item, unsigned quantity, const InventoryAccess &access);
    // All-or-nothing pickup: fill compatible stacks, then place the remainder in a free rectangle.
    InventoryResult collect(ItemHandle item, EntityId backpack, const InventoryAccess &access);
};
} // namespace d2x
