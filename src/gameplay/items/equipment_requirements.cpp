#include "gameplay/items/equipment_requirements.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/state.hpp"
#include <algorithm>

namespace d2x {
InventoryError checkEquipmentRequirements(const ItemInstance &item, const ItemDefinition &definition,
    const EquipmentActor &actor, const std::function<int()> &requirementPercent) {
    if (!item.identified) return InventoryError::Unidentified;
    if (!definition.equipment.known || definition.equipment.types.empty())
        return InventoryError::UnsupportedEquipment;
    if (!definition.equipment.requiredClass.empty() &&
        definition.equipment.requiredClass != actor.characterClass)
        return InventoryError::WrongClass;
    const int percent = requirementPercent();
    auto requirement = [&](int base) { return std::max(0, base + base * percent / 100 -
        ((item.nativeFlags & 0x400000u) ? 10 : 0)); };
    if (actor.strength < requirement(definition.base.requiredStrength.value_or(0)) ||
        actor.dexterity < requirement(definition.base.requiredDexterity.value_or(0)) ||
        actor.level < std::max({definition.base.requiredLevel.value_or(0), item.requiredLevel, item.socketRequiredLevel}))
        return InventoryError::RequirementsNotMet;
    return InventoryError::None;
}
} // namespace d2x
