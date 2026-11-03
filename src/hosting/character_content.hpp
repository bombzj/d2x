#pragma once
#include "server/runtime/prepared_rules.hpp"
namespace d2x {
struct ClassicData;
struct PersistentCharacter;
std::shared_ptr<const server::EquipmentRules> prepareEquipmentRules(const ClassicData &, const PersistentCharacter &);
void prepareCharacterRules(server::PreparedRules &, const ClassicData &, const PersistentCharacter &);
}
