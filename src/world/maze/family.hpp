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
inline FamilyRules sewerRules(int level) {
    FamilyRules rules{301, 12, {}, 12, true};
    if (level == 48)
        rules.specialRooms = {332, 345, 337};
    else if (level == 49)
        rules.specialRooms = {332, 341};
    else if (level == 65)
        rules.specialRooms = {332, 349};
    return rules;
}
} // namespace d2x::maze