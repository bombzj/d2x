#pragma once
#include "gameplay/effects/definition.hpp"
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/character/attributes.hpp"
#include <optional>
#include <variant>
#include <vector>

namespace d2x {
struct EffectHandle {
    uint64_t value = 0;
    auto operator<=>(const EffectHandle &) const = default;
};
enum class EffectUnitKind { Player, Monster, Boss };
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
} // namespace d2x
