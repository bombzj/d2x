#pragma once
#include "core/id.hpp"
#include <cstdint>
namespace d2x {
struct OpenWoundsApplication {int64_t rate{};EntityId source,credit;uint64_t until{},next{};};
}
