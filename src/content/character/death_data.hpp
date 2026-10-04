#pragma once
#include "gameplay/character/attributes.hpp"
#include <array>
#include <map>
#include <span>
#include <string>

namespace d2x {
class Archives;
class DataTable;
struct PlayerDeathData {
    struct Timing { int frames = 0, speed = 0; };
    std::map<std::string, Timing> timings;
    std::array<int, 3> experiencePenalty{};
};
PlayerDeathData loadPlayerDeathData(Archives &archives, std::span<const CharacterDefinition> characters,
                                   const DataTable &difficulties);
} // namespace d2x
