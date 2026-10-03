#pragma once
#include "core/math.hpp"
#include "gameplay/combat/identity.hpp"
#include "gameplay/combat/stats.hpp"
#include "gameplay/units/ailments.hpp"

namespace d2x {
struct CombatEffectSet;
// Ephemeral adapter; never retained across entity insertion/removal or travel.
struct CombatUnit {
    EntityId id;
    CombatIdentity identity;
    Vec *position = nullptr;
    float *life = nullptr, *mana = nullptr, *chill = nullptr;
    PeriodicDamageView poison, openWounds;
    WebSlowView webSlow;
    uint64_t *random = nullptr;
    CombatEffectSet *effects = nullptr;
    UnitCombatStats stats;
    // Body classification preserves the original player/hireling/monster
    // policies; summoned monsters retain their separate allegiance role.
    bool player = false, hireling = false, monster = false;
    explicit operator bool() const { return life != nullptr; }
    bool alive() const { return life && *life > 0; }
};
} // namespace d2x
