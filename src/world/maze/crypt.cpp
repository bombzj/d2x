#include "family.hpp"

namespace d2x::maze {
FamilyRules cryptRules(int level) {
    return {108, 8, {139, level == 18 ? 147 : level == 19 ? 151 : 143}};
}
} // namespace d2x::maze