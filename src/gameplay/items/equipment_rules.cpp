#include "equipment_rules.hpp"
#include <algorithm>

namespace d2x {
namespace {
constexpr std::array<const char *, size_t(EquipmentSlot::Count)> slotCodes{
    "head", "neck", "tors", "rarm", "larm", "rrin", "lrin", "belt", "feet", "glov",
    "rarm", "larm"};
}
const char *equipmentSlotCode(EquipmentSlot slot) {
    return size_t(slot) < slotCodes.size() ? slotCodes[size_t(slot)] : "";
}
std::optional<EquipmentSlot> equipmentSlotFromCode(std::string_view code) {
    for (size_t index = 0; index < slotCodes.size(); ++index)
        if (code == slotCodes[index])
            return EquipmentSlot(index);
    return std::nullopt;
}
bool EquipmentDefinition::isType(std::string_view type) const {
    return std::find(types.begin(), types.end(), type) != types.end();
}
bool canUseWeaponOneHanded(const EquipmentDefinition &weapon, std::string_view characterClass) {
    // ITEMS_Is1Or2Handed: this exception belongs to the player Barbarian.
    return characterClass == "bar" && weapon.oneOrTwoHanded;
}
bool requiresBothWeaponHands(const EquipmentDefinition &weapon, std::string_view characterClass) {
    return weapon.twoHanded && !canUseWeaponOneHanded(weapon, characterClass);
}
bool usesTwoHandedWeapon(const EquipmentDefinition &weapon, std::string_view characterClass, bool oppositeOccupied) {
    // INVENTORY_GetWieldType examines occupied body slots, including broken
    // equipment. Attribute eligibility does not change the wield arrangement.
    return weapon.isType("miss") || (weapon.twoHanded &&
        (!canUseWeaponOneHanded(weapon, characterClass) || !oppositeOccupied));
}
bool compatibleWeaponHands(const EquipmentDefinition &left, const EquipmentDefinition &right,
                           std::string_view characterClass) {
    // ItemMode::sub_6FC43280 handles ammunition before two-handed conflicts.
    if (!left.shoots.empty() || !right.shoots.empty())
        return (!left.shoots.empty() && right.isType(left.shoots) && !right.quiver.empty() && left.isType(right.quiver)) ||
            (!right.shoots.empty() && left.isType(right.shoots) && !left.quiver.empty() && right.isType(left.quiver));
    if (!left.quiver.empty() || !right.quiver.empty() ||
        requiresBothWeaponHands(left, characterClass) || requiresBothWeaponHands(right, characterClass)) return false;
    if (left.isType("shld") || right.isType("shld"))
        return left.isType("shld") ? right.isType("weap") : left.isType("weap");
    return left.isType("weap") && right.isType("weap") &&
        (characterClass == "bar" || (characterClass == "ass" && left.isType("h2h") && right.isType("h2h")));
}
} // namespace d2x
