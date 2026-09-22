#pragma once
#include <vector>

namespace d2x::maze {
struct FamilyRules {
    int base;
    int roomSize;
    std::vector<int> specialRooms;
};
FamilyRules caveRules(int level);
FamilyRules cryptRules(int level);
} // namespace d2x::maze