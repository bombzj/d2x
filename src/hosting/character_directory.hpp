#pragma once
#include <cstdint>
#include "content/character/actor_appearance.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct CharacterRosterEntry {
    uint64_t id{}; // Opaque identity, meaningful only in this roster revision.
    std::string name, problem, fileName;
    std::optional<unsigned> characterClass, level;
    std::optional<ActorAppearance> appearance;
    bool playable{};
    uint16_t nativeStatus{0x20};
};
struct CharacterRosterView {
    uint64_t revision{};
    std::vector<CharacterRosterEntry> characters;
};
} // namespace d2x
