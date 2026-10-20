#include "world/native_path_step.hpp"
#include "world/navigation.hpp"
#include "gameplay/units/direction.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace d2x {
namespace {
constexpr int64_t precisionScale = 1LL << 16;
int64_t precise(float coordinate) {
    return int64_t(std::llround(double(coordinate) * precisionScale));
}
Vec projected(int64_t x, int64_t y) {
    return {float(double(x) / precisionScale), float(double(y) / precisionScale)};
}
bool same(Vec a, Vec b) { return a.x == b.x && a.y == b.y; }
}
void NativePathStep::reset() { initialized_ = changedCell_ = false;direction_={}; }
Vec NativePathStep::translate(Vec position, Vec offset) {
    if (!initialized_ || !same(position, published_)) { reset(); return position + offset; }
    precisionX_ += precise(offset.x); precisionY_ += precise(offset.y);
    published_ = projected(precisionX_, precisionY_);
    return published_;
}
NativePathStepResult NativePathStep::advance(const Grid &grid, Vec position, Vec point,
                                            float distance, MovementCollisionRule rule, Vec gridOrigin) {
    NativePathStepResult result{position};
    changedCell_=false;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(distance) || distance <= 0) return result;
    if (!initialized_ || !same(position, published_)) {
        precisionX_ = precise(position.x); precisionY_ = precise(position.y);
        initialized_ = true;
    }
    const int64_t targetX = precise(point.x), targetY = precise(point.y);
    const int64_t startX = precisionX_, startY = precisionY_;
    const int64_t deltaX = targetX - precisionX_, deltaY = targetY - precisionY_;
    if (!deltaX && !deltaY) { result.reached = true; return result; }
    // D2MOO Step::PATH_GetDirectionVector and PathMisc::D2Common_10226
    // (MIT, docs/licenses/D2MOO.txt): recompute from precise coordinates on
    // each 25Hz game frame, never from a floating rendering-frame displacement.
    const auto direction=nativeDirectionVector(deltaX,deltaY);
    const int64_t magnitude = int64_t(double(distance) * precisionScale);
    int64_t velocityX = (magnitude * direction.x) >> 12;
    int64_t velocityY = (magnitude * direction.y) >> 12;
    if (!velocityX && !velocityY) return result;
    // Step::sub_6FDACEC0 clamps both axes to the precise next point together.
    const auto maximum = std::max(std::abs(velocityX), std::abs(velocityY));
    result.reached = std::abs(deltaX) <= maximum && std::abs(deltaY) <= maximum;
    if (result.reached) { velocityX = deltaX; velocityY = deltaY; }
    const int64_t nextX = precisionX_ + velocityX, nextY = precisionY_ + velocityY;
    int column = int(precisionX_ >> 16), row = int(precisionY_ >> 16);
    const int endColumn = int(nextX >> 16), endRow = int(nextY >> 16);
    int64_t sampleX = precisionX_, sampleY = precisionY_;
    // Step::sub_6FDAB810 halves the velocity until each component is <=1
    // subtile. Check only cells entered by those samples, not a DDA supercover.
    while (std::abs(velocityX) > precisionScale || std::abs(velocityY) > precisionScale) {
        velocityX >>= 1; velocityY >>= 1;
    }
    int savedSteps = 0;
    while (column != endColumn || row != endRow) {
        sampleX += velocityX; sampleY += velocityY;
        const int nextColumn = int(sampleX >> 16), nextRow = int(sampleY >> 16);
        if (nextColumn == column && nextRow == row) continue;
        const bool accepted=grid.collisionStep?
            grid.collisionStep(column-int(gridOrigin.x),row-int(gridOrigin.y),nextColumn-int(gridOrigin.x),nextRow-int(gridOrigin.y),rule):
            grid.movementClear(nextColumn-int(gridOrigin.x),nextRow-int(gridOrigin.y),rule);
        if (!accepted) {
            // Native collision stops at the center of the last accepted cell.
            precisionX_ = int64_t(column) * precisionScale + precisionScale / 2;
            precisionY_ = int64_t(row) * precisionScale + precisionScale / 2;
            result.blocked = true; result.reached = false;
            break;
        }
        column = nextColumn; row = nextRow;
        if (++savedSteps == 10) break; // D2DynamicPathStrc::PATH_MAX_STEP_LEN.
    }
    if (!result.blocked) { precisionX_ = nextX; precisionY_ = nextY; }
    published_ = projected(precisionX_, precisionY_);
    result.position = published_;
    changedCell_=(startX>>16)!=(precisionX_>>16) || (startY>>16)!=(precisionY_>>16);
    direction_=direction.index;
    const double travelledX = double(precisionX_ - startX) / precisionScale;
    const double travelledY = double(precisionY_ - startY) / precisionScale;
    result.travelled = float(std::sqrt(travelledX * travelledX + travelledY * travelledY));
    return result;
}
} // namespace d2x
