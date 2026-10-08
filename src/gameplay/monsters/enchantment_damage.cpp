#include "enchantment_damage.hpp"
#include "core/random.hpp"
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
        const int64_t low=int64_t(range.minimum)*(int64_t(1)<<shift);
        const int64_t spread=int64_t(std::max(0,range.maximum-range.minimum))*(int64_t(1)<<shift);
        return low+(spread?limitedRandom(random,uint32_t(spread)):0);
    };
    for(size_t channel=1;channel<elements.size();++channel)
        if(elements[channel].maximum) hit.channels[channel]+=roll(elements[channel],channel==5?0:8)*sourceDamage/128;
    hit.coldFrames+=uint64_t(std::max(0,cold))*unsigned(sourceDamage)/128;
    hit.poisonFrames+=unsigned(std::max(0,poison));
    if(mods.manaDamage.maximum) hit.mana+=roll(mods.manaDamage,8);
    if(mods.aura && mods.aura->element>=0 && mods.aura->elementalMultiplier>0) {
        const auto &a=*mods.aura;
        const int64_t low=int64_t(a.minimumDamage*256),spread=int64_t((a.maximumDamage-a.minimumDamage)*256);
        hit.channels[size_t(a.element)]+=(low+(spread>0?limitedRandom(random,uint32_t(spread)):0))*a.elementalMultiplier*sourceDamage/128;
    }
}
}
