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
    Texture2D rectangleTable_{}, halfTable_{}, highlightTable_{}, socketTable_{}, quarterTable_{};
    std::array<Color, 256> colors_{};
    RenderTexture2D destination_{};
    int destinationLocation_ = -1, paletteLocation_ = -1, tableLocation_ = -1, indicesLocation_ = -1, opaqueLocation_ = -1;
    void drawSprite(const Sprite *, Rectangle, Texture2D) const;
    int paletteIndex(Color) const;

  public:
    explicit PaletteBlendView(Archives &archives, int act = 0);
    ~PaletteBlendView();
    PaletteBlendView(const PaletteBlendView &) = delete;
    PaletteBlendView &operator=(const PaletteBlendView &) = delete;
    void draw(const Sprite *image, Vec position) const;
    void drawHalf(const Sprite *image, Rectangle bounds) const;
    // CelDraw modes 0/1/2/5, selected per automap cell by D2Client.
    void drawAutomap(const Sprite *image, Vec position, int mode) const;
    void drawSocket(const Sprite *image, Rectangle bounds) const;
    void drawHighlight(const Sprite *image, Rectangle bounds) const;
    Color paletteColor(Color rgb) const { return colors_[size_t(paletteIndex(rgb))]; }
    // Native DrawBox mode 0: trans[2][destination][color], with palette RGB lookup.
    void drawRectangle(Rectangle bounds, Color color) const;
    void drawLighting(Texture2D lightMap, Vec player, Vec playerScreen, Vec origin, float zoom, Color ambient) const;
};
} // namespace d2x
