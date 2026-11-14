#pragma once
#include "core/math.hpp"
#include <cstdint>

namespace d2x {
class NativePathStep;
// Per-path value state. Copies retain FP16 precision for rollback and room
// translation; collision and stepping operations belong to native_path_step.hpp.
class NativePathState {
    friend class NativePathStep;
    int64_t precisionX_{}, precisionY_{};
    Vec published_;
    bool initialized_{};
    bool changedCell_{};
    uint8_t direction_{};
  public:
    bool changedCell() const { return changedCell_; }
    uint8_t direction() const { return direction_; }
    Vec nativePosition() const { return published_; }
};
} // namespace d2x
