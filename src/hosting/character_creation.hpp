#pragma once
#include "persistence/initial_character.hpp"
#include "content/character/actor_appearance.hpp"
namespace d2x {
struct ClassicData;
ActorAppearance characterAppearance(const ClassicData &, const PersistentCharacter &);
} // namespace d2x
