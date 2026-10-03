#pragma once
#include <cstdint>

namespace d2x {
enum class Attribute { Strength, Dexterity, Vitality, Energy };
struct AttributeAllocation {
    int strength = 0, dexterity = 0, vitality = 0, energy = 0;
};
int64_t allocatedPoints(const AttributeAllocation &allocation);
bool allocateAttribute(AttributeAllocation &allocation, int &unspent, Attribute attribute);
} // namespace d2x
