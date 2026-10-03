#pragma once
#include "gameplay/character/attributes.hpp"

namespace d2x {
struct SkillPassiveSpec {
    int attackRatingPerBaseRank = 0;
    int maxResistElement = -1;
};
void applySkillPassive(CharacterModifiers &modifiers, const SkillPassiveSpec &spec,
                       int baseRank, bool suppressed);
} // namespace d2x
