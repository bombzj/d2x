#pragma once
#include "gameplay/effects/definition.hpp"
#include "gameplay/effects/unit_event.hpp"
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <optional>
#include <variant>
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
enum class NativeStateRemoval { Default,BasicAura,HolyFreeze,SkillShrine,RestoreAi,ClearTargets,Cold,Freeze };
enum class NativeStateExpiration { NonzeroDuration,EvenZeroDuration };
struct EffectVisual {
    int overlayId = -1;
};
enum class CombatEffectEvent { DamagedInMelee, AttackedInMelee, HitByMissile, DealtMeleeDamage };
struct FreezeAttacker {
    float duration = 0;
    int overlayId = -1;
    float overlayDuration = 0;
};
// Add typed actions here as their original event functions are implemented.
// Reactions belong to the effect; removal cannot leave registered callbacks.
struct ColdMeleeRetaliation {};
struct ColdMissileRetaliation {};
struct SlowOther {
    int percent = 0;
    CombatStateDefinition state;
};
struct IronMaidenRetaliation {
    int percent = 0, reducedPercent = 0, hitClass = -1;
};
struct LifeTapHealing {
    int percent = 0, overlayId = -1;
    float overlayDuration = 0;
};
using EffectAction = std::variant<FreezeAttacker, ColdMeleeRetaliation, ColdMissileRetaliation,
                                 IronMaidenRetaliation, LifeTapHealing, SlowOther>;
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
    CurseAi curseAi = CurseAi::None;
    std::vector<EffectReaction> reactions;
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
    int clearStateOnRemoval=-1; // BasicAura owner callback also clears NoManaRegen.
    bool refreshPassivesOnRemoval{};
    NativeStateRemoval nativeRemoval{NativeStateRemoval::Default};
    NativeStateExpiration nativeExpiration{NativeStateExpiration::NonzeroDuration};
    std::optional<uint64_t> modeActionRevision; // Original flag-4 list follows its owning mode.
    bool registersUnitEvents{};
    std::vector<SkillUnitEventBinding> unitEvents;
    bool nativeMissileHitGate{}; // JUSTHIT stays set until its RemoveState callback.
};
} // namespace d2x
