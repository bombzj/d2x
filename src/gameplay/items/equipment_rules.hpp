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
    Count
};
const char *equipmentSlotCode(EquipmentSlot slot);
std::optional<EquipmentSlot> equipmentSlotFromCode(std::string_view code);
struct EquipmentDefinition {
    bool known = false;
    std::array<bool, size_t(EquipmentSlot::Count)> slots{};
    std::vector<std::string> types;
    std::string requiredClass, shoots, quiver, twoHandWeaponClass;
    bool twoHanded = false, oneOrTwoHanded = false, throwable = false, repairable = false;
    bool fits(EquipmentSlot slot) const {
        return known && size_t(slot) < slots.size() && slots[size_t(slot)];
    }
    bool isType(std::string_view type) const;
};
struct EquipmentActor {
    std::string characterClass;
    int strength = 0, dexterity = 0, level = 1;
    int blockFactor = 0;
};
} // namespace d2x