#include "state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
template<class Predicate>
std::vector<RemovedCombatEffect> CombatEffectSet::erase(Predicate predicate, EffectRemoval reason) {
    std::vector<RemovedCombatEffect> removed;
    // Collect before changing storage so consumers never observe a half-removed list.
    for (const auto &effect : effects_)
        if (predicate(effect)) removed.push_back({effect, reason});
    std::erase_if(effects_, predicate);
    return removed;
}
EffectApplication CombatEffectSet::apply(CombatEffectSpec spec, EffectFrame now) {
    if (spec.state.id < 0 || spec.state.group < 0 || spec.source.definition < 0 ||
        spec.source.level < 0 || (spec.duration && *spec.duration == 0))
        throw std::invalid_argument("Invalid combat state, source or duration");
    if (nextHandle_ == std::numeric_limits<uint64_t>::max() ||
        (spec.duration && *spec.duration > std::numeric_limits<EffectFrame>::max() - now))
        throw std::overflow_error("Combat effect handle or lifetime exhausted");
    for (const auto &reaction : spec.reactions)
        std::visit([](const FreezeAttacker &action) {
            if (!std::isfinite(action.duration) || action.duration <= 0 ||
                !std::isfinite(action.overlayDuration) || action.overlayDuration < 0)
                throw std::invalid_argument("Invalid combat effect reaction");
        }, reaction.action);
    ActiveCombatEffect effect{{nextHandle_}, std::move(spec), now, {}};
    if (effect.spec.duration) effect.expiresAt = now + *effect.spec.duration;
    auto replaces = [&](const ActiveCombatEffect &existing) {
        if (effect.spec.state.group > 0 && existing.spec.state.group == effect.spec.state.group)
            return true;
        if (existing.spec.state.id != effect.spec.state.id) return false;
        switch (effect.spec.stacking) {
        case EffectStacking::ReplaceState: return true;
        case EffectStacking::ReplaceSource:
            return existing.spec.source.kind == effect.spec.source.kind &&
                   existing.spec.source.entity == effect.spec.source.entity &&
                   existing.spec.source.definition == effect.spec.source.definition;
        case EffectStacking::Independent: return false;
        }
        throw std::invalid_argument("Unknown combat effect stacking rule");
    };
    // Prepare the replacement before committing; no callbacks run while iterating.
    auto next = effects_;
    EffectApplication result{effect.handle, {}};
    for (const auto &existing : next)
        if (!existing.activeAt(now) || replaces(existing))
            result.removed.push_back({existing, existing.activeAt(now)
                ? EffectRemoval::Replaced : EffectRemoval::Expired});
    std::erase_if(next, [&](const auto &existing) { return !existing.activeAt(now) || replaces(existing); });
    next.push_back(std::move(effect));
    effects_.swap(next);
    ++nextHandle_;
    return result;
}
std::vector<RemovedCombatEffect> CombatEffectSet::expire(EffectFrame now) {
    return erase([=](const auto &effect) { return !effect.activeAt(now); }, EffectRemoval::Expired);
}
std::vector<RemovedCombatEffect> CombatEffectSet::onDeath(EffectUnitKind kind) {
    return erase([=](const auto &effect) { return !effect.spec.state.stayOnDeath.at(size_t(kind)); },
                 EffectRemoval::Death);
}
std::vector<RemovedCombatEffect> CombatEffectSet::onHit() {
    return erase([](const auto &effect) { return effect.spec.state.removeOnHit; }, EffectRemoval::Hit);
}
std::vector<RemovedCombatEffect> CombatEffectSet::remove(EffectHandle handle) {
    return erase([=](const auto &effect) { return effect.handle == handle; }, EffectRemoval::Dispelled);
}
std::vector<RemovedCombatEffect> CombatEffectSet::removeState(int stateId) {
    return erase([=](const auto &effect) { return effect.spec.state.id == stateId; }, EffectRemoval::Dispelled);
}
std::vector<RemovedCombatEffect> CombatEffectSet::removeSource(CombatEffectSource kind, EntityId source) {
    return erase([=](const auto &effect) { return effect.spec.source.kind == kind &&
                                               effect.spec.source.entity == source; }, EffectRemoval::SourceRemoved);
}
std::vector<RemovedCombatEffect> CombatEffectSet::clear() {
    return erase([](const auto &) { return true; }, EffectRemoval::Cleared);
}
bool CombatEffectSet::hasState(int stateId, EffectFrame now) const {
    return std::any_of(effects_.begin(), effects_.end(), [=](const auto &effect) {
        return effect.spec.state.id == stateId && effect.activeAt(now);
    });
}
CharacterModifiers CombatEffectSet::modifiers(EffectFrame now) const {
    CharacterModifiers result;
    for (const auto &effect : effects_)
        if (effect.activeAt(now)) mergeCharacterModifiers(result, effect.spec.modifiers);
    return result;
}
std::vector<TriggeredCombatEffect> CombatEffectSet::reactions(CombatEffectEvent event, EffectFrame now) const {
    std::vector<TriggeredCombatEffect> result;
    for (const auto &effect : effects_)
        if (effect.activeAt(now))
            for (const auto &reaction : effect.spec.reactions)
                if (reaction.event == event)
                    result.push_back({effect.handle, effect.spec.state.id, effect.spec.source, reaction.action});
    return result;
}
} // namespace d2x
