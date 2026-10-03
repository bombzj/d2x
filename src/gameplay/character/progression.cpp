#include "progression.hpp"
#include <algorithm>

namespace d2x {
bool grantCharacterExperience(CharacterProgressionContext c, uint64_t amount,
                              std::span<const uint64_t> thresholds, int statPerLevel) {
    if (!c.actor || !c.alive || !amount || thresholds.empty()) return false;
    c.experience += std::min(amount, thresholds.back() - c.experience);
    const int before = c.level;
    while (size_t(c.level + 1) < thresholds.size() && c.experience >= thresholds[size_t(c.level + 1)])
        ++c.level;
    c.unspentAttributes += (c.level - before) * statPerLevel;
    c.unspentSkills += c.level - before;
    return true;
}
bool allocateCharacterAttribute(CharacterProgressionContext c, Attribute attribute) {
    return c.actor && c.alive && allocateAttribute(c.allocated, c.unspentAttributes, attribute);
}
bool resetCharacterAttributes(CharacterProgressionContext c) {
    if (!c.actor || !c.alive || !allocatedPoints(c.allocated)) return false;
    c.unspentAttributes += allocatedPoints(c.allocated);
    c.allocated = {};
    return true;
}
} // namespace d2x
