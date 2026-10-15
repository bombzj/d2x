#pragma once
#include "alignment.hpp"
#include "core/id.hpp"

namespace d2x {
enum class CombatRelation { Unknown, Self, Allied, Neutral, Hostile };
struct CombatRelationInput {
    EntityId source, target;
    UnitAlignment sourceAlignment{UnitAlignment::Evil}, targetAlignment{UnitAlignment::Evil};
    bool bothControlled{}, sameController{}, directedHostility{}, sameParty{}, forcedHostility{};
};
CombatRelation harmfulRelation(const CombatRelationInput &input);
bool beneficialRelation(const CombatRelationInput &input);
}