#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace d2x {
struct ClassicData;
struct PersistentCharacter;
bool validCharacterName(std::string_view);
PersistentCharacter createCharacter(const ClassicData &, std::string name, unsigned characterClass, uint32_t seed, bool hardcore = false);
} // namespace d2x
