#include "use_source.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
void removeSkillUse(std::optional<NativeSkillSlot> &used,std::span<const SkillUseRemoval> removals) noexcept {
    if(!used) return;
    for(const auto &removal:removals)
        if(removal.skill==used->skill && (!removal.owner || *removal.owner==used->owner)) {used.reset();return;}
}
int nativeSkillSlotLevel(NativeSkillSlot slot,std::span<const NativeSkillSlot> order,
    const std::map<int,int> &effectiveRanks,std::span<const ChargedSkill> charges,int maximumRank) {
    if(maximumRank<=0) throw std::invalid_argument("Missing native skill level cap");
    if(std::find(order.begin(),order.end(),slot)==order.end()) return 0;
    if(!slot.owner) {
        const auto level=effectiveRanks.find(slot.skill);
        return level==effectiveRanks.end()?0:std::clamp(level->second,0,maximumRank);
    }
    const auto charge=std::find_if(charges.begin(),charges.end(),[&](const auto &entry) {
        return entry.item.id==slot.owner && entry.skill==slot.skill;
    });
    return charge==charges.end()?0:std::clamp(charge->rank,0,maximumRank);
}
std::optional<SkillUseSource> resolveSkillUseSource(int skill, uint32_t owner, bool innate,
                                                  int effectiveRank, std::span<const ChargedSkill> charges) {
    if (owner != UINT32_MAX) {
        const auto found = std::find_if(charges.begin(), charges.end(), [&](const auto &charge) {
            return charge.item.id.value == owner && charge.skill == skill && charge.charges > 0;
        });
        if (found == charges.end()) return {};
        return SkillUseSource{found->rank, *found};
    }
    return SkillUseSource{innate ? 1 : effectiveRank, {}};
}
}
