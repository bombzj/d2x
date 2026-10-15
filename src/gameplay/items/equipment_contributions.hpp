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
struct EquipmentItem;
struct EquipmentActor;
struct ItemInstance;
enum class EquipmentContributionKind { Item, NativeSetLayer, SetBonus };
struct EquipmentContribution {
    EquipmentContributionKind kind{EquipmentContributionKind::Item};
    EntityId item;
    std::string set;
    size_t instruction{}, layer{};
    bool weapon{};
    std::vector<ResolvedItemStat> stats;
};
// Content prepares immutable set facts and resolves property instructions only
// when the rule accepts that contribution. No MPQ or complete content catalog.
struct EquipmentContributionSource {
    std::span<const EquipmentSetPiece> sets;
    std::function<std::vector<ResolvedItemStat>(const ItemInstance &, int)> itemStats;
    std::function<std::vector<ResolvedItemStat>(size_t, size_t, int)> setStats;
    std::function<std::vector<ResolvedItemStat>(EntityId, size_t, int)> nativeSetStats;
    std::function<void(EntityId,std::span<const ResolvedItemStat>)> observe{};
    std::function<void(EquipmentContribution)> contribution{};
    // A native adapter samples the current posted parameterized sources after
    // each contribution. Scalar-only callers retain their typed actor input.
    std::function<EquipmentActor()> requirementActor{};
};
// Worn items are the native equip-node mask (not the posted-owner subset).
// The caller has already filtered BROKEN/NOEQUIP and inactive weapon slots.
std::vector<EquipmentContribution> equipmentSetContributions(EquipmentItem,
    std::span<const EquipmentItem> worn,const EquipmentContributionSource &,int level);
CharacterModifiers deriveEquipmentModifiers(const EquipmentLoadout &loadout,
    const EquipmentActor &baseActor, const EquipmentContributionSource &source, EntityId excludedItem = {},
    std::set<EntityId> *activeItems = nullptr);
} // namespace d2x
