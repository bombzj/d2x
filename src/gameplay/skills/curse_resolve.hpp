#pragma once
#include "curse_spec.hpp"
#include <span>

namespace d2x {
struct ActiveCombatEffect;
// Callers supply rank, target stats and immunity policy; no actor records or MPQ.
CurseSpec evaluateCurse(const CurseSpec &spec, int rank);
CharacterModifiers evaluateCurseModifiers(CharacterModifiers modifiers, const CharacterAttributes &target,
    std::span<const ActiveCombatEffect> effects, EffectFrame frame, bool diminishAgainstImmunity);
} // namespace d2x
