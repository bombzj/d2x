#pragma once
#include "gameplay/combat/identity.hpp"
#include <map>
#include <utility>

namespace d2x {
struct CombatRelations {
    std::map<std::pair<unsigned, unsigned>, Relation> factions{
        {{1, 2}, Relation::Hostile}, {{2, 1}, Relation::Hostile}};
    // Directional overrides support hostility without changing a unit's species.
    std::map<std::pair<EntityId, EntityId>, Relation> units;
};
} // namespace d2x
