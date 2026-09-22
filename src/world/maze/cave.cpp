#include "family.hpp"

namespace d2x::maze {
FamilyRules caveRules(int level) {
    FamilyRules rules{52, 24, {83, level == 8 ? 95 : 91}};
    if (level == 9)
        rules.specialRooms.push_back(99);
    if (level == 10)
        rules.specialRooms.push_back(87);
    return rules;
}
} // namespace d2x::maze