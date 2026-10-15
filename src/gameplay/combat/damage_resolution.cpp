#include "damage_resolution.hpp"
#include "mitigation.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
int piercedResistance(int resistance, int pierce) {
    return resistance < 100 ? int(std::clamp<int64_t>(int64_t(resistance) - pierce,-100,INT32_MAX)) : resistance;
}
std::optional<int32_t> staticFieldDamageFixed(int32_t life,int32_t maximumLife,
    int damagePercent,int minimumPercent,int32_t minimumDamage,int resistance) {
    const auto wholeLife=life >> 8;
    if(wholeLife<1) return {};
    if(minimumPercent && wholeLife<=nativePercentage(maximumLife >> 8,minimumPercent,100)) return {};
    auto damage=nativeShiftLeft(std::min(nativeSubtract(wholeLife,1),nativePercentage(wholeLife,damagePercent,100)),8);
    damage=std::max(minimumDamage,damage);
    if(resistance<0) damage=nativePercentage(100,damage,nativeSubtract(100,resistance));
    return damage;
}
uint64_t resistedDuration(uint64_t frames, int resistance) {
    if (frames > INT32_MAX) throw std::overflow_error("Duration exceeds native frame range");
    return uint64_t(std::max(0, nativePercentage(int32_t(frames),
        100 - std::clamp(resistance, -100, 100), 100)));
}
uint64_t coldHitDuration(uint64_t frames, int resistance, int pierce, bool cannotBeFrozen, bool halfFreezeDuration) {
    if (cannotBeFrozen) return 0;
    if (halfFreezeDuration) frames /= 2;
    return resistedDuration(frames, piercedResistance(resistance, pierce));
}
int rawResistance(const CharacterAttributes &stats, DamageType type) {
    if(stats.rawResistances) return stats.rawResistances->at(size_t(type));
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
int damageResistance(int raw,int maximumBonus,bool monsterRules,bool physical,int pierce) {
    if (pierce && (raw<100 || !monsterRules)) raw=nativeSubtract(raw,pierce);
    if(raw<=0) return std::max(raw,-100);
    if(monsterRules) return std::min(raw,100);
    const auto maximum=physical?50:std::min(nativeAdd(75,maximumBonus),95);
    return std::clamp(raw,-100,std::max(-100,maximum));
}
DamageMitigation damageMitigation(MonsterDamageType type, const CharacterAttributes &s, bool monsterRules,int pierce,bool bypass,bool sanctuary) {
    const auto &m = s.combat;
    int maximum = 0, flat = 0, absorbPercent = 0, absorb = 0;
    switch (type) {
    case MonsterDamageType::Physical:
        flat = m.flatPhysicalReduction;
        break;
    case MonsterDamageType::Magic:
        maximum = m.magicMaxResist;
        flat = m.flatMagicReduction;
        absorbPercent = m.magicAbsorbPercent; absorb = m.magicAbsorb;
        break;
    case MonsterDamageType::Fire:
        maximum = m.fireMaxResist; flat = m.flatMagicReduction;
        absorbPercent = m.fireAbsorbPercent; absorb = m.fireAbsorb;
        break;
    case MonsterDamageType::Lightning:
        maximum = m.lightningMaxResist; flat = m.flatMagicReduction;
        absorbPercent = m.lightningAbsorbPercent; absorb = m.lightningAbsorb;
        break;
    case MonsterDamageType::Cold:
        maximum = m.coldMaxResist; flat = m.flatMagicReduction;
        absorbPercent = m.coldAbsorbPercent; absorb = m.coldAbsorb;
        break;
    case MonsterDamageType::Poison:
        maximum = m.poisonMaxResist;
        break;
    }
    auto resist=damageResistance(rawResistance(s,type),maximum,monsterRules,type==DamageType::Physical,pierce);
    if(sanctuary && type==DamageType::Physical && resist>0) resist=0;
    return {resist, flat, absorbPercent, absorb,bypass};
}
} // namespace d2x
