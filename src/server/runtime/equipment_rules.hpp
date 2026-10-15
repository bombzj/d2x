#pragma once
#include "core/id.hpp"
#include "gameplay/items/modifiers.hpp"
#include "gameplay/items/equipment_set.hpp"
#include <array>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace d2x::server {
struct ItemLevelValues {
    std::vector<ResolvedItemStat> stats;
    std::array<std::vector<ResolvedItemStat>, 5> setStats;
    unsigned maximumDurability{};
};
struct EquipmentValues { std::vector<ItemLevelValues> levels; bool singleCarry{}; };
struct EquipmentRules {
    std::map<EntityId, EquipmentValues> items;
    std::vector<EquipmentSetPiece> sets;
    std::map<std::pair<size_t, size_t>, std::vector<std::vector<ResolvedItemStat>>> setBonuses;
    const ItemLevelValues &at(EntityId, int level) const;
    void includeSets(const EquipmentRules &);
};
}