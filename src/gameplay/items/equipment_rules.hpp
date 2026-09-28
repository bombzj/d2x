#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
enum class EquipmentSlot {
    Head,
    Neck,
    Torso,
    RightHand,
    LeftHand,
    RightRing,
    LeftRing,
    Belt,
    Feet,
    Gloves,
    AlternateRightHand,
    AlternateLeftHand,
    Count
};
constexpr EquipmentSlot weaponHandSlot(bool left, unsigned set) {
    return set ? (left ? EquipmentSlot::AlternateLeftHand : EquipmentSlot::AlternateRightHand)
               : (left ? EquipmentSlot::LeftHand : EquipmentSlot::RightHand);
}
constexpr bool weaponSlotActive(EquipmentSlot slot, unsigned set) {
    return (slot != EquipmentSlot::RightHand && slot != EquipmentSlot::LeftHand &&
            slot != EquipmentSlot::AlternateRightHand &&
            slot != EquipmentSlot::AlternateLeftHand) ||
           slot == weaponHandSlot(false, set) || slot == weaponHandSlot(true, set);
}
const char *equipmentSlotCode(EquipmentSlot slot);
std::optional<EquipmentSlot> equipmentSlotFromCode(std::string_view code);
struct EquipmentDefinition {
    bool known = false;
    std::array<bool, size_t(EquipmentSlot::Count)> slots{};
    std::vector<std::string> types;
    std::string requiredClass, shoots, quiver, twoHandWeaponClass;
    bool twoHanded = false, oneOrTwoHanded = false, throwable = false, repairable = false;
    bool fits(EquipmentSlot slot) const {
        if (slot == EquipmentSlot::AlternateRightHand) slot = EquipmentSlot::RightHand;
        if (slot == EquipmentSlot::AlternateLeftHand) slot = EquipmentSlot::LeftHand;
        return known && size_t(slot) < slots.size() && slots[size_t(slot)];
    }
    bool isType(std::string_view type) const;
};
struct EquipmentActor {
    std::string characterClass;
    int strength = 0, dexterity = 0, level = 1;
    int blockFactor = 0;
    unsigned weaponSet = 0;
    bool hireling = false;
};
} // namespace d2x
