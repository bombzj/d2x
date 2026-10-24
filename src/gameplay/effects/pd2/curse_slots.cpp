#include "curse_slots.hpp"
#include "gameplay/effects/values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <cmath>

namespace d2x {
int32_t pd2CursePrimaryValue(int32_t value,int32_t effectiveness) {
    const double adjusted=double(value)*(100.0-double(effectiveness))/100.0;
    if(adjusted>INT32_MAX) return INT32_MAX;
    if(adjusted<INT32_MIN) return INT32_MIN;
    return int32_t(std::trunc(adjusted));
}
Pd2CursePosting preparePd2CurseSlots(std::span<ActiveCombatEffect> effects,CombatEffectSpec &incoming,EffectFrame now) {
    // Current core CURSES posting replacement, RVA 0x36C020. Curse slots
    // are list metadata; an existing larger allowance survives another caster.
    const auto policy=*incoming.pd2CurseSlots;
    auto limit=policy.count;int count{};uint64_t oldest{},same{};bool changed{};
    for(auto current=effects.rbegin();current!=effects.rend();++current) {
        auto &effect=*current;
        if(!effect.activeAt(now) || !effect.spec.state.curse) continue;
        ++count;
        if(effect.spec.nativeValues) {
            const auto slots=effect.spec.nativeValues->find(policy.stat);
            if(slots!=effect.spec.nativeValues->end()) {
                if(slots->second && slots->second<limit) {
                    effect.spec.nativeValues->erase(slots);++effect.revision;changed=true;
                } else if(slots->second>limit) limit=slots->second;
            }
        }
        if(uint32_t(effect.nativeExpireFrame.value_or(0))<INT32_MAX) oldest=effect.handle.value;
        if(effect.spec.state.id==incoming.state.id && !same) same=effect.handle.value;
    }
    const auto replacement=policy.preserveExisting?0:same?same:count>=limit?oldest:0;
    int retainedSlots{};
    for(const auto &effect:effects) if(effect.handle.value!=replacement && effect.activeAt(now) && effect.spec.nativeValues) {
        const auto slots=effect.spec.nativeValues->find(policy.stat);
        if(slots!=effect.spec.nativeValues->end()) retainedSlots=nativeAdd(retainedSlots,slots->second);
    }
    if(retainedSlots<limit) {
        if(!incoming.nativeValues) incoming.nativeValues.emplace();
        incoming.nativeValues->insert_or_assign(policy.stat,limit);
    }
    return {replacement,changed};
}
}
