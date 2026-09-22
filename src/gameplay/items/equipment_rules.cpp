#include "equipment_rules.hpp"
#include <algorithm>

namespace d2x {
namespace {
constexpr std::array<const char *, size_t(EquipmentSlot::Count)> slotCodes{
    "head", "neck", "tors", "rarm", "larm", "rrin", "lrin", "belt", "feet", "glov"};
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
} // namespace d2x