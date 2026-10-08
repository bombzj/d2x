#include "damage_resolution.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
int64_t units(float amount) {
    if (!std::isfinite(amount)) throw std::runtime_error("Non-finite combat damage");
    if (amount <= 0) return 0;
    return int64_t(std::min(double(amount) * 256.0,
                            double(std::numeric_limits<int32_t>::max())));
}
}
int rawResistance(const CharacterAttributes &stats, DamageType type) {
    switch (type) {
    case DamageType::Physical: return stats.combat.physicalResist;
    case DamageType::Magic: return stats.combat.magicResist;
    case DamageType::Fire: return stats.fireResist;
    case DamageType::Lightning: return stats.lightningResist;
    case DamageType::Cold: return stats.coldResist;
    case DamageType::Poison: return stats.poisonResist;
    }
    return 0;
}
ResolvedDamage mitigatePlayerDamage(float amount, MonsterDamageType type,
                                    const CharacterAttributes &s) {
    const auto &m = s.combat;
    int resist = 0, flat = 0, absorbPercent = 0, absorb = 0;
    switch (type) {
    case MonsterDamageType::Physical:
        resist = std::clamp(m.physicalResist, -100, 50);
        flat = m.flatPhysicalReduction;
        break;
    case MonsterDamageType::Magic:
        resist = std::clamp(m.magicResist, -100, std::max(-100, std::min(95, 75 + m.magicMaxResist)));
        flat = m.flatMagicReduction;
        absorbPercent = m.magicAbsorbPercent; absorb = m.magicAbsorb;
        break;
    case MonsterDamageType::Fire:
        resist = s.fireResist; flat = m.flatMagicReduction;
        absorbPercent = m.fireAbsorbPercent; absorb = m.fireAbsorb;
        break;
    case MonsterDamageType::Lightning:
        resist = s.lightningResist; flat = m.flatMagicReduction;
        absorbPercent = m.lightningAbsorbPercent; absorb = m.lightningAbsorb;
        break;
    case MonsterDamageType::Cold:
        resist = s.coldResist; flat = m.flatMagicReduction;
        absorbPercent = m.coldAbsorbPercent; absorb = m.coldAbsorb;
        break;
    case MonsterDamageType::Poison:
        resist = s.poisonResist;
        break;
    }
    int64_t value = std::max<int64_t>(0, units(amount) - int64_t(flat) * 256);
    value = value * (100 - std::clamp(resist, -100, 100)) / 100;
    const int64_t percent = value * std::clamp(absorbPercent, 0, 40) / 100;
    const int64_t fixed = std::min<int64_t>(std::max<int64_t>(0, int64_t(absorb) * 256), value - percent);
    return {float(value - percent - fixed) / 256.f, float(percent + fixed) / 256.f};
}
float mitigateMonsterDamage(float amount, int resistance) {
    return float(units(amount) * (100 - std::clamp(resistance, -100, 100)) / 100) / 256.f;
}
} // namespace d2x
