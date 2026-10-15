#pragma once
#include <cstdint>

namespace d2x {
// Local scheduler progress, never a gameplay packet or a second game clock.
struct FixedStepProgress {
    uint64_t sequence{};
    uint32_t requested{},completed{};
    bool blocked{},cancelled{};
    bool pending() const noexcept {return !cancelled && completed<requested;}
};
}
