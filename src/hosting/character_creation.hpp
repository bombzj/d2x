#pragma once
#include "gameplay/character/persistent_character.hpp"
#include "content/character/actor_appearance.hpp"
#include <string_view>
namespace d2x {
struct ClassicData;
bool validCharacterName(std::string_view);
PersistentCharacter createCharacter(const ClassicData &, std::string name, unsigned characterClass, uint32_t seed);
ActorAppearance characterAppearance(const ClassicData &, const PersistentCharacter &);
} // namespace d2x
