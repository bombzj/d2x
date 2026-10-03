#include "gameplay/items/skill_sources.hpp"
#include "gameplay/items/equipment_loadout.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/state.hpp"

namespace d2x {
std::vector<ItemSkillGrant> equipmentSkillGrants(const EquipmentLoadout &loadout,
    const EquipmentActor &actor, int skill) {
    std::vector<ItemSkillGrant> result;
    for (auto slot : {weaponHandSlot(false, actor.weaponSet), weaponHandSlot(true, actor.weaponSet)}) {
        const auto entry = loadout.equipped.at(size_t(slot));
        if (!entry) continue;
        const auto &item = *entry.instance;
        if (item.quantity && item.grantedSkill == skill &&
            (!entry.definition->maxDurability || item.durability) &&
            loadout.requirements(entry, actor) == InventoryError::None)
            result.push_back({item.handle(), skill, 1});
    }
    return result;
}
} // namespace d2x
