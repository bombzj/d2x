#pragma once
#include <span>

namespace d2x {
struct CombatModifiers;
struct ItemSkillGrant;
struct SkillRankSourceRequest {
    int skill = -1, learned = 0, classRow = 0, page = 0;
    bool native = false;
};
// Caller supplies learning identity, current eligible grants and bonuses.
// No actor/monster/character/inventory lookup or action/resource mutation.
int resolveSkillSourceRank(SkillRankSourceRequest request, std::span<const ItemSkillGrant> grants,
                          const CombatModifiers &bonuses);
} // namespace d2x
