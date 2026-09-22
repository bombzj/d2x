#include "family.hpp"

namespace d2x::maze {
FamilyRules jailRules(int level) {
    FamilyRules rules{205, 12, {236}, 12, true};
    if (level == 29)
        rules.specialRooms.push_back(248);
    if (level == 30)
        rules.specialRooms.push_back(252);
    rules.specialRooms.push_back(level == 31 ? 244 : 240);
    return rules;
}
} // namespace d2x::maze