#pragma once
#include "graphics.hpp"

namespace d2x {
// Missiles.Trans=1 and Overlay.Trans=3 use the original PL2 screen table.
// Destination capture stays on the GPU and preserves the actor draw order.
class PaletteBlendView {
    Shader shader_{};
    Texture2D palette_{}, screenTable_{}, paletteIndices_{};
    RenderTexture2D destination_{};
    int destinationLocation_ = -1, paletteLocation_ = -1, tableLocation_ = -1, indicesLocation_ = -1;

  public:
    explicit PaletteBlendView(Archives &archives);
    ~PaletteBlendView();
    PaletteBlendView(const PaletteBlendView &) = delete;
    PaletteBlendView &operator=(const PaletteBlendView &) = delete;
    void draw(const Sprite *image, Vec position) const;
};
} // namespace d2x
