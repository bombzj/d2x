#pragma once
#include "gameplay/character/attributes.hpp"
#include <memory>

namespace d2x {
struct AmazonPassiveSpec;
struct BarbarianPassiveSpec;
struct SkillPassiveSpec {
    std::shared_ptr<const AmazonPassiveSpec> amazon;
    std::shared_ptr<const BarbarianPassiveSpec> barbarian;
    int attackRatingPerBaseRank = 0;
    int maxResistElement = -1;
    int suppressedByState = -1;
};
void applySkillPassive(CharacterModifiers &modifiers, const SkillPassiveSpec &spec,
                       int baseRank, bool suppressed);
void applyBarbarianPassive(CharacterModifiers &, const BarbarianPassiveSpec &, int rank);
} // namespace d2x
