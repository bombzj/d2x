#pragma once
#include "gameplay/character/attributes.hpp"
#include <memory>

namespace d2x {
struct AmazonPassiveSpec;
struct SkillPassiveSpec {
    std::shared_ptr<const AmazonPassiveSpec> amazon;
    int attackRatingPerBaseRank = 0;
    int maxResistElement = -1;
    int suppressedByState = -1;
};
void applySkillPassive(CharacterModifiers &modifiers, const SkillPassiveSpec &spec,
                       int baseRank, bool suppressed);
} // namespace d2x
