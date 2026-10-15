#pragma once
#include "values.hpp"
#include "gameplay/events/frame_queue.hpp"
#include <span>
#include <vector>
#include <functional>
#include <memory>
#include <map>
#include <set>

namespace d2x {
struct EffectApplication {
    EffectHandle handle;
    std::vector<RemovedCombatEffect> removed;
    bool accepted = true;
    bool reused = false;
    std::vector<int> clearedStates;
};
// Owned by the affected unit. No global player, resource, device, or save access.
// Public views are immutable; callers consume removal records after mutation.
class CombatEffectSet {
    struct Storage {uint64_t nextHandle=1,passiveRefresh=0,staminaRestore=0,controlReset=0,targetReset=0,nextUnitEvent=1;std::vector<ActiveCombatEffect> effects;std::map<int,bool> states;FrameEventQueue expirationEvents;std::vector<TriggeredSkillUnitEvent> skillEvents;};
    std::shared_ptr<Storage> storage_=std::make_shared<Storage>();
    void detach();
    void afterRemoval(const ActiveCombatEffect &,const std::function<void()> &,std::optional<EffectUnitKind> dead={});
    void scheduleExpiration(EffectFrame now,int32_t nativeFrame);
    void renewExpiration(ActiveCombatEffect &,const CombatEffectSpec &,EffectFrame);
    void restoreStamina(const std::function<void()> &);
    void resetControl(const std::function<void()> &);
    void resetTargets(const std::function<void()> &);
    void unregisterUnitEvents(int state);
    std::vector<RemovedCombatEffect> removeDueNative(EffectFrame,const std::function<void()> &,std::optional<EffectUnitKind>);
    template<class Predicate>
    std::vector<RemovedCombatEffect> erase(Predicate predicate, EffectRemoval reason,const std::function<void()> &observe={},std::optional<EffectUnitKind> dead={});
  public:
    CombatEffectSet()=default;
    CombatEffectSet(const CombatEffectSet &) noexcept=default;
    CombatEffectSet &operator=(const CombatEffectSet &) noexcept=default;
    // Keep moved-from candidates readable; shared storage is still detached
    // before mutation. Copies and publication never allocate.
    CombatEffectSet(CombatEffectSet &&other) noexcept:storage_(other.storage_) {}
    CombatEffectSet &operator=(CombatEffectSet &&other) noexcept {storage_=other.storage_;return *this;}
    std::span<const ActiveCombatEffect> entries() const { return storage_->effects; }
    size_t size() const { return storage_->effects.size(); }
    bool hasUnitEvents() const {return !storage_->skillEvents.empty();}
    bool sharesSnapshot(const CombatEffectSet &other) const noexcept {return storage_==other.storage_;}
    std::vector<FrameEvent> expirationEvents() const {return storage_->expirationEvents.events({});}
    // Overlay a previously prepared edit onto a later candidate. Unchanged
    // baseline lists/flags retain intervening writes; fresh handles are remapped.
    std::map<EffectHandle,EffectHandle> overlay(const CombatEffectSet &baseline,const CombatEffectSet &edit);
    // observe reads each withdrawal, then the posted/reused list. It must not
    // mutate this set; authority consumers call it on private candidates.
    EffectApplication apply(CombatEffectSpec effect, EffectFrame now,const std::function<void()> &observe={});
    // Borrowed only until this set changes. Allocation/reuse and ordered
    // native SetStat writes are separate operations, as in the original.
    const ActiveCombatEffect *find(EffectHandle) const;
    // D2Common GetStatListFromUnitAndState: newest posted list first.
    const ActiveCombatEffect *findState(int stateId) const;
    // afterWrite synchronously observes this set; it must not mutate it.
    bool writeNativeStats(EffectHandle,std::span<const StatValue>,const StatRuleCatalog &,const CombatEffectSpec &,const std::function<void()> &afterWrite={},bool refreshEvents=true);
    // Removal observers see remaining posted sources after each withdrawal;
    // they must not mutate this set or retain a borrowed element.
    std::vector<RemovedCombatEffect> expire(EffectFrame now,const std::function<void()> &observe={},std::optional<EffectUnitKind> dead={});
    std::vector<RemovedCombatEffect> expireOrdinary(EffectFrame now,const std::function<void()> &observe={},std::optional<EffectUnitKind> dead={});
    std::vector<RemovedCombatEffect> expireEvent(FrameEventHandle,EffectFrame,const std::function<void()> &observe={},std::optional<EffectUnitKind> dead={});
    bool hasDueExpiration(EffectFrame) const;
    std::vector<RemovedCombatEffect> onDeath(EffectUnitKind kind,const std::function<void()> &observe={});
    std::vector<int> clearNonDeathStates(EffectUnitKind,const CombatStateRules &,EffectFrame,const std::function<void()> &observe={});
    std::vector<RemovedCombatEffect> onHit(const std::function<void()> &observe={});
    std::vector<RemovedCombatEffect> remove(EffectHandle handle,const std::function<void()> &observe={});
    std::vector<RemovedCombatEffect> removeState(int stateId,const std::function<void()> &observe={});
    bool hasEndedModeEffects(std::optional<uint64_t> actionRevision) const;
    std::vector<RemovedCombatEffect> endModeEffects(std::optional<uint64_t> actionRevision,const std::function<void()> &observe={});
    bool shortenCurableCurses(EffectFrame now, int remainingPercent);
    bool setExpiresAt(EffectHandle,EffectFrame,EffectFrame now);
    std::vector<RemovedCombatEffect> removeSource(CombatEffectSource kind, EntityId source,const std::function<void()> &observe={});
    std::vector<RemovedCombatEffect> clear(const std::function<void()> &observe={});
    bool hasState(int stateId, EffectFrame now) const;
    bool setNativeState(int stateId,bool active);
    std::set<int> stateIds(EffectFrame,bool includePendingRemoval=false) const;
    uint64_t passiveRefreshEpoch() const noexcept {return storage_->passiveRefresh;}
    uint64_t staminaRestoreEpoch() const noexcept {return storage_->staminaRestore;}
    uint64_t controlResetEpoch() const noexcept {return storage_->controlReset;}
    uint64_t targetResetEpoch() const noexcept {return storage_->targetReset;}
    void refreshPassiveSkills(const std::function<void()> &observe={});
    CharacterModifiers modifiers(EffectFrame now) const;
    std::vector<TriggeredCombatEffect> reactions(CombatEffectEvent event, EffectFrame now) const;
    std::vector<TriggeredSkillUnitEvent> unitEvents(NativeUnitEvent,EffectFrame) const;
    bool registered(const TriggeredSkillUnitEvent &) const;
    void registerUnitEvents(int state,EffectSource,std::span<const SkillUnitEventBinding>,EffectHandle handle={});
};
} // namespace d2x
