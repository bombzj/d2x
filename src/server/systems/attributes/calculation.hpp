#pragma once
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/contracts.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/items/equipment_loadout.hpp"
#include <set>
namespace d2x::server::attributes {
struct Totals {
    CharacterAttributes character;
    EquipmentStats equipment;
    std::map<int, int> skillRanks;
    std::set<EntityId> activeEquipment;
    uint64_t sourceRevision{};
};
EquipmentLoadout loadout(const PersistentCharacter &, const ItemCatalog &, const EquipmentRules &);
Totals calculate(const CharacterDefinition &, const PersistentCharacter &, const ItemCatalog &,
    const EquipmentRules &, const CharacterRules &, EntityId excluded = {});
}
