#pragma once
#include "core/id.hpp"

namespace d2x {
// Species, allegiance and ownership are independent. Faction IDs are open ended;
// an unconfigured relationship is neutral, never implicitly hostile.
enum class CombatRole { Player, Monster, Hireling, Summon };
enum class Relation { Neutral, Allied, Hostile };
struct CombatIdentity {
    unsigned faction = 0;
    EntityId owner;
    unsigned party = 0;
    CombatRole role = CombatRole::Monster;
    bool attackable = true;
};
} // namespace d2x
