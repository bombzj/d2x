#pragma once
#include "gameplay/character/persistent_character.hpp"
namespace d2x::server {
// Allocate a fresh instance namespace, including nested socket items, corpses
// and the saved golem item. No old runtime handle survives admission.
PersistentCharacter admitCharacter(PersistentCharacter);
}
