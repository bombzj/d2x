#pragma once
#include <cstdint>
#include <array>

namespace d2x {
struct GraphicsDiagnostics {
    // Unique owned textures, attributed to their first upload path.
    std::array<uint64_t, 3> textureCounts{}, textureBytes{}; // direct, single, composite
    std::array<uint64_t,4> atlasBytesByUse{}; // terrain, actor (including shadows), effect, item
    uint64_t atlasBytes{}, atlasPages{}, atlasUploads{}, atlasUploadBytes{}, atlasEvictions{}, atlasPeakBytes{}, directionBytes{}, frameRecords{};
    uint64_t owners{},textures{},logicalRgbaBytes{},decodedAnimations{},decodedFrames{},decodedPixelBytes{};
};
// Render-thread observation of Graphics-owned assets, not driver/VRAM usage.
GraphicsDiagnostics graphicsDiagnostics();
void beginGraphicsFrame();
}
