#pragma once
#include "classic_data.hpp"
#include "gameplay/combat/stat_modifiers.hpp"

namespace d2x {
void applyEquipmentCombatProperty(const ClassicData &content, const PropertyRange &property,
                                  int roll, EntityId item, bool weapon, CombatModifiers &mods);
} // namespace d2x
