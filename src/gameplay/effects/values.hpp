#pragma once
#include "spec.hpp"
#include <cstddef>

namespace d2x {
struct ActiveCombatEffect {
    EffectHandle handle;
    CombatEffectSpec spec;
    EffectFrame startedAt = 0;
    std::optional<EffectFrame> expiresAt;
    uint64_t revision{}; // Runtime list mutation identity, never persisted.
    std::optional<int32_t> nativeExpireFrame;
    uint64_t unitEventOrder{}; // SUNITEVENT_Register head insertion, independent of stat-list order.
    bool activeAt(EffectFrame frame) const { return spec.nativeState || !expiresAt || frame < *expiresAt; }
};
struct RemovedCombatEffect {
    ActiveCombatEffect effect;
    EffectRemoval reason;
};
struct TriggeredCombatEffect {
    EffectHandle handle;
    int stateId;
    EffectSource source;
    EffectAction action;
};
struct TriggeredSkillUnitEvent {
    EffectHandle handle;
    int state{-1};
    EffectSource source;
    SkillUnitEventBinding binding;
    uint64_t registration{};
    size_t slot{};
};
}
