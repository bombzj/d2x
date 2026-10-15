#pragma once
#include "state.hpp"

namespace d2x {
struct NativeStatusPolicy {NativeStateRemoval removal{NativeStateRemoval::Default};int clearState{-1};};
// SUnitDmg status lists preserve their first owner. Reapplication and ordered
// SetStat writes are separate; cold/freeze callers decide whether to extend.
EffectApplication applyNativeStatus(CombatEffectSet &,CombatStateDefinition,EffectSource,
    EffectFrame until,EffectFrame now,const StatRuleCatalog &,std::span<const StatValue>,
    NativeStatusPolicy={},const std::function<void()> &observe={});
}
