#pragma once
#include <span>
#include <optional>
#include <set>
#include "core/id.hpp"

namespace d2x {
struct CombatModifiers;
struct ItemSkillGrant;
struct StatSnapshot;struct StatRuleCatalog;
struct SkillFormulaCatalog;
struct NativeSkillSlot {
    int skill{};
    EntityId owner; // Zero represents the native unowned (-1 GUID) entry.
    auto operator<=>(const NativeSkillSlot &) const = default;
};
struct SkillRankSourceRequest {
    int skill = -1, learned = 0, classRow = 0, page = 0;
    bool native = false;
};
// Caller supplies learning identity, current eligible grants and bonuses.
// No actor/monster/character/inventory lookup or action/resource mutation.
int resolveSkillSourceRank(SkillRankSourceRequest request, std::span<const ItemSkillGrant> grants,
                          const CombatModifiers &bonuses);
std::optional<int> resolveNativePlayerSkillRank(SkillRankSourceRequest,const StatSnapshot &,const StatRuleCatalog &,
    int elementType,int maximum,bool innate=false,int levelBonus=0,bool assigned=false);
int nativeShrineSkillBonus(const SkillFormulaCatalog &,const std::set<int> &states);
} // namespace d2x
