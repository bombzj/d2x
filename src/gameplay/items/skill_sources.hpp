#pragma once
#include "gameplay/items/handle.hpp"
#include <vector>

namespace d2x {
struct EquipmentLoadout;
struct EquipmentActor;
struct ItemSkillGrant {
    ItemHandle item;
    int skill = -1, rank = 0;
};
// Existing starter grants only. Charged/triggered item skills require separately
// verified instance representation and consumption; book charges are unrelated.
std::vector<ItemSkillGrant> equipmentSkillGrants(const EquipmentLoadout &loadout,
    const EquipmentActor &actor, int skill);
} // namespace d2x
