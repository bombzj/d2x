#pragma once
#include "gameplay/model/definitions.hpp"
#include "core/math.hpp"
#include "core/id.hpp"
#include "gameplay/combat/identity.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/combat/weapon_attack.hpp"
#include "gameplay/units/restoration.hpp"
#include <deque>
#include <string>
#include <cstdint>

namespace d2x {
// The quest reward is a character-owned hireling. Position and route are
// session state; native D2S records identity, experience, equipment and death status.
struct HirelingState {
    EntityId id; // Runtime identity; D2S identifies a mercenary by its seed.
    CombatIdentity allegiance{1, {}, 0, CombatRole::Hireling};
    int sourceRow = -1;
    int classId = -1;
    std::string nameKey;
    int level = 0;
    float hp = 0;
    Vec pos, look{1, 0};
    std::deque<Vec> route;
    bool moving = false;
    float attackTimer = 0;
    std::optional<WeaponAttackState> attack;
    float thinkTimer = 0, hitTime = 0, hitDuration = 0, deathAge = 0;
    float baseHitDuration = 0, deathDuration = 0, animationTime = 0, animationRate = 0;
    float chill = 0, poisonRemaining = 0, poisonPerSecond = 0;
    EntityId poisonSource, openWoundsSource;
    float openWoundsRemaining = 0, openWoundsPerSecond = 0;
    float webSlowRemaining = 0;
    int webSlowPercent = 0, collisionSize = 0, attackBias = 0;
    RegionId corpseRegion = RegionId::Encampment;
    bool corpseVisible = false;
    std::deque<ResourceRestoration> healing;
    CombatEffectSet combatEffects;
    uint64_t experience = 0;
    uint32_t seed = 0;
    uint64_t combatRandom = 0;
    bool active() const { return sourceRow >= 0 && hp > 0; }
};
} // namespace d2x
