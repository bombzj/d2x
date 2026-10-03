#pragma once
#include "gameplay/effects/spec.hpp"
#include <span>
#include <vector>

namespace d2x {
struct ActiveCombatEffect {
    EffectHandle handle;
    CombatEffectSpec spec;
    EffectFrame startedAt = 0;
    std::optional<EffectFrame> expiresAt;
    bool activeAt(EffectFrame frame) const { return !expiresAt || frame < *expiresAt; }
};
struct RemovedCombatEffect {
    ActiveCombatEffect effect;
    EffectRemoval reason;
};
struct EffectApplication {
    EffectHandle handle;
    std::vector<RemovedCombatEffect> removed;
};
struct TriggeredCombatEffect {
    EffectHandle handle;
    int stateId;
    EffectSource source;
    EffectAction action;
};
// Owned by the affected unit. No global player, resource, device, or save access.
// Public views are immutable; callers consume removal records after mutation.
class CombatEffectSet {
    uint64_t nextHandle_ = 1;
    std::vector<ActiveCombatEffect> effects_;
    template<class Predicate>
    std::vector<RemovedCombatEffect> erase(Predicate predicate, EffectRemoval reason);
  public:
    std::span<const ActiveCombatEffect> entries() const { return effects_; }
    size_t size() const { return effects_.size(); }
    EffectApplication apply(CombatEffectSpec effect, EffectFrame now);
    std::vector<RemovedCombatEffect> expire(EffectFrame now);
    std::vector<RemovedCombatEffect> onDeath(EffectUnitKind kind);
    std::vector<RemovedCombatEffect> onHit();
    std::vector<RemovedCombatEffect> remove(EffectHandle handle);
    std::vector<RemovedCombatEffect> removeState(int stateId);
    void shortenCurableCurses(EffectFrame now, int remainingPercent);
    std::vector<RemovedCombatEffect> removeSource(CombatEffectSource kind, EntityId source);
    std::vector<RemovedCombatEffect> clear();
    bool hasState(int stateId, EffectFrame now) const;
    CharacterModifiers modifiers(EffectFrame now) const;
    std::vector<TriggeredCombatEffect> reactions(CombatEffectEvent event, EffectFrame now) const;
};
} // namespace d2x
