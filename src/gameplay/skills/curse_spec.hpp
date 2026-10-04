#pragma once
#include "gameplay/effects/definition.hpp"
#include "gameplay/character/attributes.hpp"

namespace d2x {
struct CurseSpec {
    CombatStateDefinition state;
    int targetFilter = 0; // Skills.aurafilter: original player/monster type bits.
    int radius = 0, radiusPerLevel = 0, frames = 0, framesPerLevel = 0;
    CharacterModifiers modifiers;
    CurseAi ai = CurseAi::None;
    int resistMinimum = 0, resistMaximum = 0;
    int reflectPercent = 0, reflectPerLevel = 0;
    int reflectReducedDivisor = 1, reflectHitClass = -1;
    int lifeTapPercent = 0, lifeTapPerLevel = 0;
    int healOverlay = -1;
    float healOverlayDuration = 0;
};
} // namespace d2x
