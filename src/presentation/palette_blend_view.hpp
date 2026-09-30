#pragma once
#include "graphics.hpp"

namespace d2x {
// Original PL2 screen blending and 32 intensity transforms share the palette.
// Destination capture stays on the GPU; see LIGHTING.md for spatial/color limits.
class PaletteBlendView {
    Shader shader_{};
    Shader lightingShader_{};
    Texture2D palette_{}, screenTable_{}, lightTable_{}, paletteIndices_{};
    RenderTexture2D destination_{};
    int destinationLocation_ = -1, paletteLocation_ = -1, tableLocation_ = -1, indicesLocation_ = -1;

  public:
    explicit PaletteBlendView(Archives &archives);
    ~PaletteBlendView();
    PaletteBlendView(const PaletteBlendView &) = delete;
    PaletteBlendView &operator=(const PaletteBlendView &) = delete;
    void draw(const Sprite *image, Vec position) const;
    void drawLighting(Texture2D lightMap, Vec player, Vec playerScreen, Vec origin, float zoom, Color ambient) const;
};
} // namespace d2x
