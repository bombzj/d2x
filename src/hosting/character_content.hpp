#pragma once
#include "server/runtime/prepared_rules.hpp"
#include "gameplay/character/persistent_character.hpp"
namespace d2x {
struct ClassicData;
void prepareCharacterRules(server::PreparedRules &, const ClassicData &, const PersistentCharacter &);
}
