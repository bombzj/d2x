#include "native_status.hpp"
#include <stdexcept>

namespace d2x {
EffectApplication applyNativeStatus(CombatEffectSet &states,CombatStateDefinition state,EffectSource source,
    EffectFrame until,EffectFrame now,const StatRuleCatalog &rules,std::span<const StatValue> writes,
    NativeStatusPolicy policy,const std::function<void()> &observe) {
    if(state.id<0 || until<now) throw std::invalid_argument("Invalid native status deadline");
    // A damage list is not a selected skill or an aura source. SUnitDmg
    // supplies zero skill metadata; OpenWounds supplies skill 0 / level 1.
    source.kind=CombatEffectSource::NativeState;
    CombatEffectSpec spec;spec.state=state;spec.source=source;spec.stacking=EffectStacking::ReuseState;
    spec.nativeDirectAllocation=true;spec.nativeState=true;spec.duration=until-now;spec.nativeExpiration=NativeStateExpiration::EvenZeroDuration;
    spec.nativeRemoval=policy.removal;spec.clearStateOnRemoval=policy.clearState;
    spec.refreshPassivesOnRemoval=policy.removal==NativeStateRemoval::Default;
    spec.nativeValues.emplace();
    const auto application=states.apply(spec,now,observe);
    if(application.accepted && !states.writeNativeStats(application.handle,writes,rules,spec,observe))
        throw std::logic_error("Missing posted native status list");
    return application;
}
}
