#pragma once
#include "gameplay/character/persistent_character.hpp"

namespace d2x::server {
struct CorpseView { PlayerCorpse corpse; PersistentCharacter equipment; uint64_t revision{}; };
}
