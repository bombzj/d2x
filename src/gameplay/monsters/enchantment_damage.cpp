#include "enchantment_damage.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
namespace d2x {
void addMonsterEnchantmentDamage(MonsterHit &hit,const MonsterEnchantment &mods,int sourceDamage,uint64_t &random,MonsterEnchantmentDamageState &state) {
    auto elements=mods.elements;
    auto cold=mods.coldFrames,poison=mods.poisonFrames;
    if(mods.has(27)) {
        constexpr std::array<size_t,5> channels{2,3,1,4,5};
        const auto channel=channels[limitedRandom(random,5)];state.spectral[channel]=mods.spectralDamage;
        if(channel==4) state.coldFrames=std::min(INT32_MAX-40,state.coldFrames)+40;
        if(channel==5) state.poisonFrames=std::min(INT32_MAX-40,state.poisonFrames)+40;
    }
    for(size_t channel=1;channel<elements.size();++channel) if(state.spectral[channel]) elements[channel]=*state.spectral[channel];
    cold=int(std::min<int64_t>(INT32_MAX,int64_t(cold)+state.coldFrames));
    poison=int(std::min<int64_t>(INT32_MAX,int64_t(poison)+state.poisonFrames));
    auto roll=[&](AttackDamageRange range,int shift) {
        const auto low=nativeShiftLeft(range.minimum,unsigned(shift));
        const auto spread=nativeShiftLeft(std::max(0,nativeSubtract(range.maximum,range.minimum)),unsigned(shift));
        return nativeAdd(low,spread>0?int32_t(limitedRandom(random,uint32_t(spread))):0);
    };
    for(size_t channel=1;channel<elements.size();++channel)
        if(elements[channel].maximum) hit.channels[channel]=nativeAdd(nativeChecked(hit.channels[channel]),nativeMultiplyDivide(roll(elements[channel],channel==5?0:8),sourceDamage,128));
    hit.coldFrames+=uint64_t(std::max(0,cold))*unsigned(sourceDamage)/128;
    hit.poisonFrames+=unsigned(std::max(0,poison));
    if(mods.manaDamage.maximum) hit.mana=nativeAdd(nativeChecked(hit.mana),roll(mods.manaDamage,8));
    // Aura attack stats are posted on the actual unit and consumed by
    // rollMonsterUnitHit. Do not roll/add the prepared aura range again.
}
}
