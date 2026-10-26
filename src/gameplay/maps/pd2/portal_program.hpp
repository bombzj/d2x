#pragma once
#include "core/math.hpp"
#include <optional>

namespace d2x::pd2 {
struct MapPortalProgram {
    int destination{},tile{},definition{},sourceMode{},destinationMode{};
    Vec offset{};bool mutual{};
    std::optional<Vec> levelOffset{}; // Native level origin, independent of spawn tiles.
    bool destinationFreeSpace{};
};
}
