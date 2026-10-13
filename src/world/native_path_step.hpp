#pragma once
#include "core/math.hpp"
#include "world/collision.hpp"
#include <cstdint>

namespace d2x {
struct Grid;
struct NativePathStepResult {
    Vec position;
    float travelled{};
    bool reached{}, blocked{};
};
// Precise position belongs to one path, never to the grid.
// Inputs/outputs use cell centers in the caller's coordinate frame; gridOrigin
// locates collision cells in that frame. Vec is only the published projection;
// fractional FP16 position survives successive steps and room translations.
class NativePathStep {
    int64_t precisionX_{}, precisionY_{};
    Vec published_;
    bool initialized_{};
  public:
    static constexpr float seconds = 1.f / 25.f;
    void reset();
    Vec translate(Vec position, Vec offset);
    NativePathStepResult advance(const Grid &grid, Vec position, Vec point,
        float distance, MovementCollisionRule rule = playerMovement, Vec gridOrigin = {});
};
} // namespace d2x
