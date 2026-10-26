#pragma once
#include "gameplay/character/persistent_character.hpp"
#include <string_view>

namespace d2x {
struct ClassicData;
bool validCharacterName(std::string_view);
PersistentCharacter createCharacter(const ClassicData &, std::string name, unsigned characterClass, uint32_t seed, bool hardcore = false);
} // namespace d2x
