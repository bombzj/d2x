#pragma once
#include "gameplay/items/skill_sources.hpp"
#include "rank_sources.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <span>

namespace d2x {
struct SkillUseSource {
    int rank{};
    std::optional<ChargedSkill> charged;
};
struct SkillUseRemoval {
    int skill{};
    // Missing owner matches every instance of this ID (native charged removal).
    // Present zero owner matches only the unowned learned/granted instance.
    std::optional<EntityId> owner{};
};
void removeSkillUse(std::optional<NativeSkillSlot> &,std::span<const SkillUseRemoval>) noexcept;
int nativeSkillSlotLevel(NativeSkillSlot,std::span<const NativeSkillSlot>,
    const std::map<int,int> &effectiveRanks,std::span<const ChargedSkill>,int maximumRank);
std::optional<SkillUseSource> resolveSkillUseSource(int skill, uint32_t owner, bool innate,
                                                  int effectiveRank, std::span<const ChargedSkill> charges);
}
