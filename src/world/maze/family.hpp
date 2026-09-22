#pragma once
#include <vector>

namespace d2x::maze {
struct FamilyRules {
    int base;
    int roomSize;
    std::vector<int> specialRooms;
    int roomHeight = roomSize;
    bool initialRing = false;
};
FamilyRules caveRules(int level);
FamilyRules cryptRules(int level);
FamilyRules barracksRules();
FamilyRules jailRules(int level);
FamilyRules catacombsRules(int level);
} // namespace d2x::maze