#pragma once
#include "system.hpp"
#include "server/player_state.hpp"

namespace d2x::server::inventory {
// Short-lived value draft. Never retains a live inventory or writes PlayerStore.
struct Edit {
    InventoryState inventory;
    std::vector<ItemChange> changes;
    unsigned weaponSet{};
    std::vector<ItemInstance> spilled{};
};
DomainResult<Edit> plan(const PlayerState &, const Request &, const ItemCatalog &, const EquipmentRules &, const CharacterRules &, bool storage = false, bool cube = false);
namespace detail {
struct Draft {
    const PlayerState &player;
    const ItemCatalog &catalog;
    const EquipmentRules &rules;
    const CharacterRules &characterRules;
    Edit edit;
    bool storage = false;
    explicit Draft(const PlayerState &p, const ItemCatalog &c, const EquipmentRules &r, const CharacterRules &cr)
        : player(p), catalog(c), rules(r), characterRules(cr), edit{p.persistent.inventory, {}, p.persistent.player.weaponSet} {}
    const PlayerContainers &containers() const { return player.persistent.containers; }
    const ItemInstance *resolve(ItemHandle) const;
    const ContainerState *container(EntityId) const;
    EntityId at(ContainerLocation) const;
    EntityId equipped(EquipmentSlot) const;
    bool owned(ContainerLocation) const;
    bool fits(EntityId, ContainerLocation, EntityId ignore = {}) const;
    std::optional<ContainerLocation> space(EntityId, EntityId) const;
    DomainStatus move(EntityId, ContainerLocation);
    DomainStatus resizeBelt(int rows);
    DomainStatus equipment(const EquipItem &, EquipmentMode);
    DomainResult<bool> autoEquip(EntityId);
    DomainStatus qualified(EntityId) const;
    DomainStatus merge(const MergeStacks &);
    unsigned stackSpace(const ItemInstance &,const ItemInstance &) const;
    DomainStatus loadBook(const LoadBook &);
    DomainStatus mergeCarried(EntityId);
};
DomainResult<transactions::WorldEdit> prepareSpill(const PlayerState &, const Edit &, EquipmentRules &, items::System &);
}
}
