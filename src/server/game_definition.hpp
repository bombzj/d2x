#pragma once
#include "server/area_store.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/character/persistent_character.hpp"

namespace d2x::server {
struct GameDefinition {
    GameSettings settings;
    AreaDefinition area;
    CharacterDefinition character;
    PersistentCharacter persistent;
    PreparedRules rules;
};
}
