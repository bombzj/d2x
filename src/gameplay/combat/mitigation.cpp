#include "mitigation.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x {
FixedDamageResult mitigateDamageFixed(int32_t amount, DamageMitigation mitigation) {
    // SUNITDMG_ApplyResistancesAndAbsorb deliberately preserves a negative
    // reduced channel until the channels are summed. Do not clamp it here.
    if (amount <= 0) return {};
    const int resistance = mitigation.bypass ? std::min(mitigation.resistance, 0)
                                             : mitigation.resistance;
    auto value = mitigation.bypass ? amount
        : nativeSubtract(amount, nativeShiftLeft(mitigation.flatReduction, 8));
    if (value > 0 && resistance)
        value = nativePercentage(value, 100 - std::clamp(resistance, -100, 100), 100);
    if (mitigation.bypass) return {value, 0};
    int32_t absorbed = 0;
    if (mitigation.absorbPercent > 0) {
        const auto percent = nativePercentage(value, std::min(mitigation.absorbPercent, 40), 100);
        value = nativeSubtract(value, percent);
        absorbed = percent;
    }
    const auto fixed = nativeShiftLeft(mitigation.flatAbsorb, 8);
    if (fixed > 0) {
        const auto stopped = std::min(fixed, value);
        value = nativeSubtract(value, stopped);
        absorbed = nativeAdd(absorbed, stopped);
    }
    return {value, absorbed};
}
int64_t resistDamageFixed(int64_t amount, int resistance) {
    return mitigateDamageFixed(nativeChecked(amount), {resistance}).dealt;
}
int32_t FixedDamageChannelsResult::total() const {
    return nativeAdd(direct,nativeChecked(channels[5].dealt));
}
FixedDamageChannelsResult mitigateDamageChannels(const std::array<int64_t,6> &damage,
    const std::array<DamageMitigation,6> &profile) {
    FixedDamageChannelsResult result;
    // SUnitDmg table order: physical, fire, lightning, cold, magic, poison.
    constexpr std::array<size_t,6> order{0,2,3,4,1,5};
    for(const auto channel:order) {
        const auto value=mitigateDamageFixed(nativeChecked(damage[channel]),profile[channel]);
        result.channels[channel]=value;
        if(channel!=5) result.direct=nativeAdd(result.direct,nativeChecked(value.dealt));
        result.absorbed=nativeAdd(result.absorbed,nativeChecked(value.absorbed));
    }
    return result;
}
}
