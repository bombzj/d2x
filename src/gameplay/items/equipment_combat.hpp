#pragma once
#include "gameplay/items/modifiers.hpp"
#include "core/id.hpp"
#include "gameplay/combat/stat_modifiers.hpp"

namespace d2x {
void applyEquipmentStat(const ResolvedItemStat &stat, EntityId item, bool weapon, CombatModifiers &mods);
} // namespace d2x
