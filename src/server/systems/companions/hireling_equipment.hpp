#pragma once
#include "preparation.hpp"
#include "server/player_state.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/equipment_stats.hpp"

namespace d2x::server::companions {
struct HirelingEquipment {
    EquipmentActor actor;
    CharacterModifiers modifiers;
    EquipmentStats equipment;
    int life{};
};
EquipmentLoadout hirelingLoadout(const PersistentCharacter &,const ItemCatalog &,const EquipmentRules &);
HirelingEquipment calculateHirelingEquipment(const PersistentCharacter &,const ItemCatalog &,
    const EquipmentRules &,const PreparedHireling &,EntityId excluded={},const std::set<int> &states={});
}
