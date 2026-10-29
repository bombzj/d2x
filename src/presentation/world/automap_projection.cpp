#include "automap_projection.hpp"
#include <cstdint>

namespace d2x {
AutomapProjection::AutomapProjection(int width, int height, bool large, Vec miniOrigin,
        float sideShift, Vec observer, Vec pan) : pan_(pan), divisor_(large ? 10 : 20), large_(large) {
    // Original 1.13c 3F860 / 60250: divide camera and cells separately,
    // truncating toward zero; do not divide a pre-subtracted float delta.
    const auto projected = project(observer);
    const Vec camera{float(int(projected.x) - width / 2), float(int(projected.y) - height / 2 + 16)};
    const Vec cleared = large ? Vec{} : Vec{float(width / 3) - miniOrigin.x - 16,
        float(height / 3) - miniOrigin.y - 16};
    origin_ = {float(int(camera.x) / divisor_ - width / 2 + 40) - sideShift + cleared.x,
        float(int(camera.y) / divisor_ - (height - 40) / 2 + 15) + cleared.y};
}
Vec AutomapProjection::cell(Vec world) const {
    const auto pixel = project(world);
    return Vec{float(int(pixel.x) / divisor_), float(int(pixel.y) / divisor_)} - origin_ + pan_;
}
Vec AutomapProjection::bhPoint(Vec nativePosition) const {
    // BH Drawing/Hook.cpp::ScreenToAutomap takes integer unit coordinates,
    // scales by 32, divides X by 2 and Y by 4, then by the map divisor.
    // 64-bit intermediates retain that order without signed multiplication UB.
    const int64_t x = int64_t(nativePosition.x) * 32, y = int64_t(nativePosition.y) * 32;
    Vec at = Vec{float((x - y) / 2 / divisor_), float((x + y) / 4 / divisor_)} - origin_ + pan_ + Vec{8, -8};
    if (!large_) at = at + Vec{-1, 5};
    return at;
}
} // namespace d2x
