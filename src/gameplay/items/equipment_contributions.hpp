#pragma once
#include "gameplay/character/attributes.hpp"
#include "gameplay/items/modifiers.hpp"
#include "core/id.hpp"
#include "gameplay/items/equipment_set.hpp"
#include <functional>
#include <span>
#include <set>
#include <vector>

namespace d2x {
struct EquipmentLoadout;
struct EquipmentActor;
struct ItemInstance;
// Content prepares immutable set facts and resolves property instructions only
// when the rule accepts that contribution. No MPQ or complete content catalog.
struct EquipmentContributionSource {
    std::span<const EquipmentSetPiece> sets;
    std::function<std::vector<ResolvedItemStat>(const ItemInstance &, int)> itemStats;
    std::function<std::vector<ResolvedItemStat>(size_t, size_t, int)> setStats;
    std::function<std::vector<ResolvedItemStat>(EntityId, size_t, int)> nativeSetStats;
    std::function<void(EntityId,std::span<const ResolvedItemStat>)> observe{};
};
CharacterModifiers deriveEquipmentModifiers(const EquipmentLoadout &loadout,
    const EquipmentActor &baseActor, const EquipmentContributionSource &source, EntityId excludedItem = {},
    std::set<EntityId> *activeItems = nullptr);
} // namespace d2x
