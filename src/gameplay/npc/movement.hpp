#pragma once
#include "core/id.hpp"
#include <cstdint>
namespace d2x {
struct Region;
void advanceNpcPaths(Region &region, EntityId pending, EntityId engaged, uint64_t &random, float dt);
} // namespace d2x
