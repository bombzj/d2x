#pragma once
#include "presentation/graphics/graphics.hpp"
#include <array>

namespace d2x {
// Original PL2 screen/rectangle blending and 32 intensity transforms share the palette.
// Destination capture stays on the GPU; see LIGHTING.md for spatial/color limits.
class PaletteBlendView {
    Shader shader_{};
    Shader lightingShader_{};
    Shader rectangleShader_{};
    Texture2D palette_{}, screenTable_{}, lightTable_{}, paletteIndices_{};
    Texture2D rectangleTable_{};
    std::array<Color, 256> colors_{};
    RenderTexture2D destination_{};
    int destinationLocation_ = -1, paletteLocation_ = -1, tableLocation_ = -1, indicesLocation_ = -1;

  public:
    explicit PaletteBlendView(Archives &archives, int act = 0);
    ~PaletteBlendView();
    PaletteBlendView(const PaletteBlendView &) = delete;
    PaletteBlendView &operator=(const PaletteBlendView &) = delete;
    void draw(const Sprite *image, Vec position) const;
    // Native DrawBox mode 0: trans[2][destination][color], with palette RGB lookup.
    void drawRectangle(Rectangle bounds, Color color) const;
    void drawLighting(Texture2D lightMap, Vec player, Vec playerScreen, Vec origin, float zoom, Color ambient) const;
};
} // namespace d2x
