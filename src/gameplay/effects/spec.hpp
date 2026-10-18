#pragma once
#include "gameplay/effects/definition.hpp"
#include "gameplay/effects/unit_event.hpp"
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <optional>
#include <vector>

namespace d2x {
struct EffectHandle {
    uint64_t value = 0;
    auto operator<=>(const EffectHandle &) const = default;
};
enum class EffectUnitKind { Player, Monster, Boss };
enum class CombatEffectSource { Skill, Monster, Shrine, Item, Environment,NativeState };
struct EffectSource {
    CombatEffectSource kind = CombatEffectSource::Skill;
    EntityId entity;
    int definition = -1, level = 0;
    auto operator<=>(const EffectSource &) const = default;
};
// The caller must choose a verified reapplication rule. A nonzero state group
// is exclusive on the recipient, independent of source or stacking policy.
enum class EffectStacking { ReplaceState, ReplaceSource, Independent, AuraLevel, CurseLevel, SkillLevel, ReuseState };
enum class EffectRemoval { Expired, Replaced, Death, Hit, Dispelled, SourceRemoved, Cleared };
enum class NativeStateRemoval { Default,BasicAura,HolyFreeze,SkillShrine,RestoreAi,ClearTargets,Cold,Freeze,Shape,Pregnant };
enum class NativeStateExpiration { NonzeroDuration,EvenZeroDuration };
struct EffectVisual {
    int overlayId = -1;
};
struct CombatEffectSpec {
    CombatStateDefinition state;
    EffectSource source;
    EffectStacking stacking = EffectStacking::ReplaceState;
    std::optional<EffectFrame> duration; // No value means source-controlled lifetime.
    CharacterModifiers modifiers;
    EffectVisual visual;
    bool restoreStaminaOnRemoval = false;
    CurseAi curseAi = CurseAi::None;
    // Native fixed-point physical shield pool, separate from elemental healing absorption.
    int64_t physicalShield = 0, physicalShieldMaximum = 0;
    // Native SetStat values, already in ItemStatCost units. When present this
    // list is authoritative; typed modifiers remain procedure projections.
    std::optional<std::map<StatKey,int32_t>> nativeValues;
    // These raw stat views are sampled from the sole mutable shield pools;
    // nativeValues does not retain another live copy of their values.
    std::optional<StatKey> physicalShieldStat,physicalShieldMaximumStat;
    bool nativeDirectAllocation{}; // Direct D2Common list allocation bypasses the curse/group posting policy.
    bool nativeState{}; // Explicit unit bit, independent of this list's expiry.
    std::vector<int> removalGroupStates; // Native shape callback walks States in index order.
    int clearStateOnRemoval=-1; // BasicAura owner callback also clears NoManaRegen.
    bool refreshPassivesOnRemoval{};
    NativeStateRemoval nativeRemoval{NativeStateRemoval::Default};
    NativeStateExpiration nativeExpiration{NativeStateExpiration::NonzeroDuration};
    std::optional<uint64_t> modeActionRevision; // Original flag-4 list follows its owning mode.
    bool registersUnitEvents{};
    std::vector<SkillUnitEventBinding> unitEvents;
    bool nativeMissileHitGate{}; // JUSTHIT stays set until its RemoveState callback.
    bool nativeLifetime() const {return nativeState || (nativeDirectAllocation && state.id<0);}
};
} // namespace d2x
