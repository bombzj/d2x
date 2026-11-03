#pragma once
#include <cstdint>

namespace d2x::server {
struct UiFact {
    uint8_t action{};
    enum class Kind { Action, Cinematic } kind{Kind::Action};
};
}
