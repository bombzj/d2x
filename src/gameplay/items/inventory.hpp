#pragma once
#include "definitions.hpp"
#include "operations.hpp"
#include "modifiers.hpp"
#include <functional>
#include <set>

namespace d2x {
class InventoryService {
    friend class GameSessionImpl;
    EntityIds &ids_;
    ItemCatalog catalog_;
    InventoryState state_;
    std::function<std::vector<ResolvedItemStat>(const ItemInstance &)> itemProperties_;
    // World collision query only; item occupancy is checked against this service's state,
    // including private equipment transaction drafts.
    std::function<bool(const GroundLocation &, const GroundLocation &)> groundPlacement_;
    std::set<int32_t> singleCarryUniques_;
    struct ReplenishTimer { float elapsed = 0; bool repeated = false; };
    std::map<std::pair<EntityId, bool>, ReplenishTimer> replenishTimers_;
    Cell stashDimensions_;
    Cell cubeDimensions_;
    void validateSnapshot(const InventoryState &state, const PlayerContainers &containers,
                          EntityId player) const;
    InventoryError checkHandle(ItemHandle handle) const;
    InventoryError checkAccess(const ItemLocation &location, const InventoryAccess &access) const;
    InventoryError checkDestinationAccess(const ItemDestination &destination,
                                          const InventoryAccess &access) const;
    InventoryError checkCarryLimit(const ItemInstance &item, const ItemLocation &destination,
                                   const InventoryState &state, EntityId ignore = {}) const;
    InventoryError checkPlacement(const ItemDefinition &definition, const ItemLocation &location,
                                  EntityId ignore = {}, EntityId alsoIgnore = {}) const;
    InventoryError resolve(const ItemDefinition &definition, const ItemDestination &destination,
                           ItemLocation &location, EntityId ignore = {},
                           std::optional<Vec> groundOrigin = std::nullopt) const;
    bool stackablesEqual(const ItemInstance &first, const ItemInstance &second) const;
    bool overlaps(const ItemDefinition &a, const ItemLocation &aPosition, const ItemDefinition &b,
                  const ItemLocation &bPosition) const;
    InventoryResult planTransfer(const TransferItem &command, const InventoryAccess &access) const;
    InventoryResult planBelt(const EquipBelt &command, const PlayerContainers &containers,
                             const InventoryAccess &access, const EquipmentActor &actor,
                             InventoryState *replacement) const;
    InventoryResult planEquipment(const EquipItem &command, const PlayerContainers &containers,
                                  const InventoryAccess &access, const EquipmentActor &actor,
                                  InventoryState *replacement) const;

  public:
    InventoryService(EntityIds &ids, ItemCatalog catalog, Cell stashDimensions, Cell cubeDimensions)
        : ids_(ids), catalog_(std::move(catalog)), stashDimensions_(stashDimensions),
          cubeDimensions_(cubeDimensions) {}
    InventoryService(const InventoryService &) = delete;
    InventoryService &operator=(const InventoryService &) = delete;
    const InventoryState &state() const { return state_; }
    const ItemCatalog &catalog() const { return catalog_; }
    int propertyValue(const ItemInstance &item, std::string_view stat) const;
    unsigned maximumDurability(const ItemInstance &item) const;
    unsigned maximumStack(const ItemInstance &item) const;
    bool retainsEmptyStack(const ItemInstance &item) const;
    InventoryResult replenish(float dt);
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
                           const InventoryAccess &access, const EquipmentActor &actor) const;
    InventoryResult equipBelt(const EquipBelt &command, const PlayerContainers &containers,
                              const InventoryAccess &access, const EquipmentActor &actor);
    InventoryError equipmentRequirements(ItemHandle item, const EquipmentActor &actor) const;
    EntityId equipped(const PlayerContainers &containers, EquipmentSlot slot) const;
    InventoryError preview(const EquipItem &command, const PlayerContainers &containers,
                           const InventoryAccess &access, const EquipmentActor &actor) const;
    InventoryResult equip(const EquipItem &command, const PlayerContainers &containers,
                          const InventoryAccess &access, const EquipmentActor &actor);
    InventoryResult wearEquipment(const PlayerContainers &containers, EntityId weapon, bool defending,
                    uint64_t &randomState, unsigned weaponSet);
    std::optional<Cell> beltSpace(EntityId belt, std::string_view code, bool automaticPickup) const;
    InventoryError previewDrink(ItemHandle item, const InventoryAccess &access) const;
    InventoryResult drink(ItemHandle item, const InventoryAccess &access);

    // Trusted gameplay creation APIs. UI commands cannot mint items or containers.
    EntityId createContainer(ContainerSpec specification);
    PlayerContainers createPlayerContainers(EntityId player);
    InventoryResult createItem(std::string_view definition, unsigned quantity,
                               const ItemDestination &destination, unsigned level = 1,
                               const ItemGeneration &generation = {},
                               std::optional<Vec> groundOrigin = std::nullopt);
    // Trusted reward/crafting replacement. Drafts consumption, creation, RNG and IDs;
    // a rejected replacement leaves the original item and random stream intact.
    InventoryResult replaceItem(ItemHandle source, std::string_view definition,
                                const ItemDestination &destination, const InventoryAccess &access,
                                unsigned level = 1, const ItemGeneration &generation = {},
                                std::optional<uint64_t> preparedRandom = std::nullopt);
    InventoryResult move(const MoveItem &command, const InventoryAccess &access);
    InventoryResult swap(const SwapItems &command, const InventoryAccess &access);
    InventoryResult split(const SplitStack &command, const InventoryAccess &access);
    InventoryResult merge(const MergeStacks &command, const InventoryAccess &access);
    InventoryError preview(const LoadBook &command, const InventoryAccess &access) const;
    InventoryResult loadBook(const LoadBook &command, const InventoryAccess &access);
    InventoryResult consumeBookCharge(ItemHandle book, const InventoryAccess &access);
    InventoryResult consume(ItemHandle item, unsigned quantity, const InventoryAccess &access);
    // Trusted combat consumption; ordinary UI consume cannot address equipment slots.
    InventoryResult consumeEquipped(EntityId item, const PlayerContainers &containers);
    // Automatic pickup keeps successful merges even when the remainder cannot fit.
    InventoryResult collect(ItemHandle item, const PlayerContainers &containers, const InventoryAccess &access);
};
} // namespace d2x
