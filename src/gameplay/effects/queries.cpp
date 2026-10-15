#include "queries.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x {
bool hasCombatState(std::span<const ActiveCombatEffect> effects, int stateId, EffectFrame frame) {
    return std::any_of(effects.begin(), effects.end(), [=](const auto &effect) {
        return effect.spec.state.id == stateId && effect.activeAt(frame);
    });
}
CharacterModifiers combatEffectModifiers(std::span<const ActiveCombatEffect> effects, EffectFrame frame,bool includePendingRemoval) {
    CharacterModifiers result;
    for (const auto &effect : effects)
        if (includePendingRemoval || effect.spec.nativeState || effect.activeAt(frame)) mergeCharacterModifiers(result, effect.spec.modifiers);
    return result;
}
bool hasCombatLifeState(std::span<const ActiveCombatEffect> effects,EffectFrame frame) {
    return std::any_of(effects.begin(),effects.end(),[=](const auto &effect){return effect.activeAt(frame) && effect.spec.state.life;});
}
std::optional<std::map<StatKey,int32_t>> combatEffectNativeValues(const CombatEffectSpec &effect) {
    auto values=effect.nativeValues;
    if(effect.physicalShieldStat || effect.physicalShieldMaximumStat) {
        if(!values) values.emplace();
        if(effect.physicalShieldStat) values->insert_or_assign(*effect.physicalShieldStat,nativeChecked(effect.physicalShield));
        if(effect.physicalShieldMaximumStat) values->insert_or_assign(*effect.physicalShieldMaximumStat,nativeChecked(effect.physicalShieldMaximum));
    }
    return values;
}
std::vector<ModifierStatSource> combatEffectStatSources(std::span<const ActiveCombatEffect> effects,EffectFrame frame,bool includePendingRemoval) {
    std::vector<ModifierStatSource> result;
    for(const auto &effect:effects) if(includePendingRemoval || effect.spec.nativeState || effect.activeAt(frame))
        result.push_back({{StatSourceKind::State,effect.spec.source.entity,effect.spec.state.id,effect.handle.value},
            effect.spec.modifiers,combatEffectNativeValues(effect.spec)});
    return result;
}
std::vector<TriggeredCombatEffect> combatEffectReactions(std::span<const ActiveCombatEffect> effects,
                                                      CombatEffectEvent event, EffectFrame frame) {
    std::vector<TriggeredCombatEffect> result;
    for (const auto &effect : effects)
        if (effect.activeAt(frame))
            for (const auto &reaction : effect.spec.reactions)
                if (reaction.event == event)
                    result.push_back({effect.handle, effect.spec.state.id, effect.spec.source, reaction.action});
    return result;
}
}
