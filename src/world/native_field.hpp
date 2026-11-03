#pragma once
#include "core/bytes.hpp"

namespace d2x {
// Prepared DATA/GLOBAL/expfield.d2. The native lookup has a 256-byte stride.
struct CollisionField {int width{},height{};Bytes directions;};
}
