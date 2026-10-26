#pragma once
#include "core/math.hpp"

namespace d2x::pd2 {
struct MapPortalProgram {
    int destination{},tile{},definition{},sourceMode{},destinationMode{};
    Vec offset{};bool mutual{};
};
}
