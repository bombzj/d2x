#include "potions.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/effects/native_status.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <stdexcept>
#include <cmath>
#include <limits>
namespace d2x {
int32_t potionRestorationFixed(const PotionDefinition &potion, std::string_view playerClass) {
    const auto amount=nativeShiftLeft(potion.amount,8);
    // D2Common ITEMS_GetBonusLifeBasedOnClass / ITEMS_GetBonusManaBasedOnClass.
    // pSpell03 shifts recovery values to 8.8 before the x1.5 operation.
    if (potion.kind == PotionKind::Healing) {
        if (playerClass.empty() || playerClass == "bar") return nativeMultiply(amount,2);
        if (playerClass == "ama" || playerClass == "pal" || playerClass == "ass") return nativeAdd(amount,amount >> 1);
    } else if (potion.kind == PotionKind::Mana) {
        if (playerClass == "sor" || playerClass == "nec" || playerClass == "dru") return nativeMultiply(amount,2);
        if (playerClass == "ama" || playerClass == "pal" || playerClass == "ass") return nativeAdd(amount,amount >> 1);
    }
    return amount;
}
int64_t rollPotionRestoration(const PotionDefinition &potion, std::string_view playerClass, int attribute, uint64_t &random) {
    auto value=potionRestorationFixed(potion,playerClass);
    if(value<=0) return 0;
    if (attribute > 0) {
        const auto chance = rollRandom(random) % 100;
        if (chance < limitedRandom(random, uint32_t(attribute)) / 2) value=nativeMultiply(value,2);
    }
    return value;
}
EffectApplication applyPotionState(CombatEffectSet &states,const PotionDefinition &potion,EntityId drinker,EffectFrame now,
    const StatRuleCatalog &rules,const CombatStateRules &definitions,std::optional<int32_t> restoration,
    const std::function<void()> &observe,const std::function<void(StatValue)> &afterWrite) {
    if(potion.state.id<0 || !potion.durationFrames) throw std::invalid_argument("Missing native potion state inputs");
    const bool recovery=potion.kind==PotionKind::Healing || potion.kind==PotionKind::Mana;
    if(recovery && !restoration) throw std::invalid_argument("Missing native potion restoration amount");
    std::vector<RemovedCombatEffect> removed;
    const auto withdraw=[&](int state) {auto entries=states.removeState(state,observe);removed.insert(removed.end(),entries.begin(),entries.end());};
    const auto key=StatKey{rules.names.at(potion.kind==PotionKind::Mana?"manarecovery":"hpregen"),0};
    int32_t remaining=0,oldRate=0;
    for(const auto &effect:states.entries()) if(effect.spec.state.id==potion.state.id) {
        if(effect.nativeExpireFrame) remaining=nativeSubtract(*effect.nativeExpireFrame,nativeNarrow(uint32_t(now)));
        else if(effect.expiresAt) remaining=nativeSubtract(nativeNarrow(uint32_t(*effect.expiresAt)),nativeNarrow(uint32_t(now)));
        if(effect.spec.nativeValues) {
            const auto value=effect.spec.nativeValues->find(key);if(value!=effect.spec.nativeValues->end()) oldRate=value->second;
        }
        break;
    }
    for(const int state:potion.cureStates) if(state>=0) withdraw(state);
    if(!recovery && potion.state.group) for(const int state:states.stateIds(now,true))
        if(state!=potion.state.id && definitions.at(state).group==potion.state.group) withdraw(state);
    const auto duration=nativeAdd(nativeNarrow(uint32_t(potion.durationFrames)),remaining);
    CombatEffectSpec spec;spec.state=potion.state;spec.source={CombatEffectSource::Item,drinker,0,0};
    spec.nativeDirectAllocation=true;spec.nativeState=true;spec.nativeExpiration=NativeStateExpiration::EvenZeroDuration;
    spec.duration=uint32_t(duration);spec.stacking=EffectStacking::ReuseState;spec.refreshPassivesOnRemoval=true;spec.nativeValues.emplace();
    auto installed=states.apply(spec,now,observe);
    installed.removed.insert(installed.removed.begin(),removed.begin(),removed.end());
    if(!installed.accepted) return installed;
    const auto write=[&](StatValue value) {
        if(!states.writeNativeStats(installed.handle,std::span{&value,size_t{1}},rules,spec,observe))
            throw std::logic_error("Missing native potion stat list");
        if(afterWrite) afterWrite(value);
    };
    if(recovery) {
        const auto total=nativeAdd(*restoration,nativeMultiply(remaining,oldRate));
        if(duration>0) write({key,nativeDivide(total,duration)});
    } else for(const auto value:potion.nativeWrites) if(value.value) write(value);
    return installed;
}
}
