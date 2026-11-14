#pragma once
#include "world/native_path_state.hpp"
#include "world/collision.hpp"
#include <cstdint>
#include <optional>

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
  public:
    NativePathStep() = delete;
    static constexpr float seconds = 1.f / 25.f;
    static void reset(NativePathState &state);
    static Vec stop(NativePathState &state, Vec position);
    static std::optional<uint8_t> directionTo(const NativePathState &state, Vec position, Vec point);
    static Vec translate(NativePathState &state, Vec position, Vec offset);
    static NativePathStepResult advance(NativePathState &state, const Grid &grid, Vec position, Vec point,
        float distance, MovementCollisionRule rule = playerMovement, Vec gridOrigin = {});
};
} // namespace d2x
