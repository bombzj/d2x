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
inline FamilyRules tombRules(int level) {
    FamilyRules rules{413, 16, {}};
    if (level >= 55 && level <= 58)
        rules.specialRooms.push_back(448);
    if (level == 57)
        rules.specialRooms.push_back(476);
    if (level == 59)
        rules.specialRooms.insert(rules.specialRooms.end(), {472, 464, 452});
    if (level == 60)
        rules.specialRooms.push_back(456);
    return rules;
}
inline FamilyRules lairRules(int level) {
    return {481, 10, level == 64 ? std::vector<int>{} : std::vector<int>{501, 497}, 10, true};
}
} // namespace d2x::maze