#include "gameplay/items/equipment_requirements.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cstdint>

namespace d2x {
InventoryError checkEquipmentRequirements(const ItemInstance &item, const ItemDefinition &definition,
    const EquipmentActor &actor, const std::function<int()> &requirementPercent) {
    if (!item.identified) return InventoryError::Unidentified;
    if (!definition.equipment.known || definition.equipment.types.empty())
        return InventoryError::UnsupportedEquipment;
    // ITEMS_CheckRequirements tests the primary base type, not stackability
    // or the inherited type closure. Empty ammunition still has item stats.
    // D2X stores a book's native QUANTITY in charges; quantity is its single
    // inventory instance count (see D2S mapping and LoadBook).
    if(definition.base.type=="book" && !item.charges) return InventoryError::RequirementsNotMet;
    if(!definition.equipment.requiredClass.empty()) {
        const bool playerClass=!actor.hireling && actor.nativeClass<0 && definition.equipment.requiredClass==actor.characterClass;
        // Original 1.13c RVA27040..27059: only monster classes 0x230/0x231
        // accept the ItemTypes.Class barbarian exception.
        const bool barbarianMercenary=actor.nativeClass>=0x230 && actor.nativeClass<=0x231 && definition.equipment.requiredClass=="bar";
        if(!playerClass && !barbarianMercenary) return InventoryError::WrongClass;
    }
    const int percent = requirementPercent();
    const auto requirement=[&](int base) {
        const auto adjusted=nativeAdd(base,nativePercentage(base,percent,100));
        return (item.nativeFlags&0x400000u)?nativeSubtract(adjusted,10):adjusted;
    };
    // ITEMS_CheckStatRequirement rejects zero or negative unit attributes even
    // when the adjusted requirement is zero/negative; it does not clamp it.
    if(actor.strength<=0 || actor.dexterity<=0 || actor.strength < requirement(definition.base.requiredStrength.value_or(0)) ||
        actor.dexterity < requirement(definition.base.requiredDexterity.value_or(0)) ||
        actor.level < std::max({definition.base.requiredLevel.value_or(0), item.requiredLevel, item.socketRequiredLevel}))
        return InventoryError::RequirementsNotMet;
    return InventoryError::None;
}
} // namespace d2x
