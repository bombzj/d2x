#pragma once
#include <cstdint>

namespace d2x {
struct MissileDamageRange {
    int32_t minimum{}, maximum{};
};
MissileDamageRange missileDamageRange(float minimum, float maximum);
MissileDamageRange fixedMissileDamageRange(int64_t minimum, int64_t maximum);
MissileDamageRange scaleMissileDamage(MissileDamageRange range, int percent);
MissileDamageRange addMissileDamage(MissileDamageRange range, MissileDamageRange addition);
int32_t rollMissileDamage(MissileDamageRange range, uint64_t &random);
int32_t rollMissileDamageValue(MissileDamageRange,int32_t mastery,uint64_t &random);
}