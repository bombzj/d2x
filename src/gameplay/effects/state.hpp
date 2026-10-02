#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/character/attributes.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace d2x {
using EffectFrame = uint64_t; // Absolute 25 Hz simulation frame; never wall-clock time.
struct EffectHandle {
    uint64_t value = 0;
    auto operator<=>(const EffectHandle &) const = default;
};
enum class EffectUnitKind { Player, Monster, Boss };
enum class CurseAi { None, DimVision, Terror, Confuse, Attract };
// Imported States.txt fields. These describe a state, not a particular skill.
struct CombatStateDefinition {
    int id = -1, group = 0;
    bool removeOnHit = false;
    std::array<bool, 3> stayOnDeath{};
    bool staminaBarBlue = false;
    bool curse = false;
    bool hideOnDeath = false, shatterOnDeath = false, corpseUnselectable = false;
    bool curable = false;
};
enum class CombatEffectSource { Skill, Monster, Shrine, Item, Environment };
struct EffectSource {
    CombatEffectSource kind = CombatEffectSource::Skill;
    EntityId entity;
    int definition = -1, level = 0;
    auto operator<=>(const EffectSource &) const = default;
};
// The caller must choose a verified reapplication rule. A nonzero state group
// is exclusive on the recipient, independent of source or stacking policy.
enum class EffectStacking { ReplaceState, ReplaceSource, Independent, AuraLevel };
enum class EffectRemoval { Expired, Replaced, Death, Hit, Dispelled, SourceRemoved, Cleared };
struct EffectVisual {
    int overlayId = -1;
};
enum class CombatEffectEvent { DamagedInMelee, AttackedInMelee, HitByMissile };
struct FreezeAttacker {
    float duration = 0;
    int overlayId = -1;
    float overlayDuration = 0;
};
// Add typed actions here as their original event functions are implemented.
// Reactions belong to the effect; removal cannot leave registered callbacks.
struct ColdMeleeRetaliation {};
struct ColdMissileRetaliation {};
using EffectAction = std::variant<FreezeAttacker, ColdMeleeRetaliation, ColdMissileRetaliation>;
struct EffectReaction {
    CombatEffectEvent event;
    EffectAction action;
};
struct CombatEffectSpec {
    CombatStateDefinition state;
    EffectSource source;
    EffectStacking stacking = EffectStacking::ReplaceState;
    std::optional<EffectFrame> duration; // No value means source-controlled lifetime.
    CharacterModifiers modifiers;
    EffectVisual visual;
    bool restoreStaminaOnRemoval = false;
    int lifeTapOverlay = -1;
    float lifeTapOverlayDuration = 0;
    CurseAi curseAi = CurseAi::None;
    Vec curseCenter;
    std::vector<EffectReaction> reactions;
};
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
