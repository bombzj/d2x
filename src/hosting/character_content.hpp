#pragma once
#include "server/runtime/prepared_rules.hpp"
#include "gameplay/character/persistent_character.hpp"
namespace d2x {
struct ClassicData;
std::shared_ptr<const server::EquipmentRules> prepareEquipmentRules(const ClassicData &, const PersistentCharacter &);
void prepareCharacterRules(server::PreparedRules &, const ClassicData &, const PersistentCharacter &);
}
