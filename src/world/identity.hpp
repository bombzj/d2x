#pragma once
#include <string>

namespace d2x {
// Native Levels.txt IDs. Template previews occupy a separate range (10000 + Def).
enum class RegionId { Encampment = 1 };
struct RegionDefinition {
    RegionId id;
    std::string name, mapPath;
    bool safe = false;
};
} // namespace d2x
