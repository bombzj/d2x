#pragma once
#include <cstdint>
#include <array>

namespace d2x {
struct DamageMitigation {
    int resistance{};
    int flatReduction{};
    int absorbPercent{};
    int flatAbsorb{};
    bool bypass{};
};
struct FixedDamageResult {
    int64_t dealt{};
    int64_t absorbed{};
};
FixedDamageResult mitigateDamageFixed(int32_t amount, DamageMitigation mitigation);
int64_t resistDamageFixed(int64_t amount, int resistance);
struct FixedDamageChannelsResult {
    std::array<FixedDamageResult,6> channels{};
    int32_t direct{},absorbed{};
    int32_t total() const;
};
FixedDamageChannelsResult mitigateDamageChannels(const std::array<int64_t,6> &damage,
    const std::array<DamageMitigation,6> &profile);
}
