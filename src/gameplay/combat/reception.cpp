#include "reception.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x {
int64_t subtractContactResource(int64_t current, int64_t damage) {
    if (damage <= 0) return std::max<int64_t>(0, current);
    if (current <= damage) return 0;
    const auto remaining = current - damage;
    return remaining < 256 ? 0 : remaining;
}
DamageReception receiveDamageChannels(const std::array<int64_t,6> &damage,const CharacterAttributes &defenses,
    DamageReception result,bool monsterRules,const std::array<int,6> &pierce,bool bypass,bool sanctuary) {
    std::array<DamageMitigation,6> profile{};
    for(size_t channel=0;channel<profile.size();++channel)
        profile[channel]=damageMitigation(DamageType(channel),defenses,monsterRules,pierce[channel],bypass,sanctuary);
    result.coldResistance=profile[size_t(DamageType::Cold)].resistance;
    const auto mitigated=mitigateDamageChannels(damage,profile);
    result.channels=mitigated.channels;result.dealt=mitigated.total();result.absorbed=mitigated.absorbed;
    if(result.drain.shieldEligible) result.dealt=nativeAdd(nativeChecked(result.dealt),nativeChecked(result.drain.life));
    return result;
}
}
