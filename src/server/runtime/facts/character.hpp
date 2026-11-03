#pragma once
#include "gameplay/character/persistent_character.hpp"
#include "server/systems/attributes/calculation.hpp"
#include <optional>

namespace d2x::server {
struct CharacterFact {
    CharacterRecord before, after;
    attributes::Totals previous, current;
    std::optional<bool> selectedHand{}; // Explicit selection also confirms an unchanged value.
    bool explicitResources{}; // Native UpdateAttribute callbacks; ordinary changes use compact sampling.
};
}
