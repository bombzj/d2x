#pragma once
#include "presentation/graphics/graphics.hpp"
#include <array>
#include <span>

namespace d2x {
// Original PL2 screen/rectangle blending and 32 intensity transforms share the palette.
// Destination capture stays on the GPU; see LIGHTING.md for spatial/color limits.
class PaletteBlendView {
    Shader shader_{};
    Shader lightingShader_{};
    Shader litBlendShader_{};
    Shader wallBlendShader_{};
    std::array<int,14> lightingLocations_{};
    std::array<int,5> litBlendLocations_{};
    std::array<int,6> wallBlendLocations_{};
    Shader rectangleShader_{};
    Texture2D palette_{}, screenTable_{}, lightTable_{}, paletteIndices_{};
    Texture2D rectangleTable_{}, halfTable_{}, highlightTable_{}, socketTable_{}, quarterTable_{};
    std::array<Color, 256> colors_{};
    RenderTexture2D destination_{};
    Texture2D worldLightMap_{};
    Vec lightPlayer_{}, lightPlayerScreen_{}, lightOrigin_{};
    float lightZoom_ = 1;
    Color lightAmbient_{};
    bool fullBright_ = false;
    mutable bool terrainBatch_{}, terrainShaderActive_{};
    mutable int terrainSampleCount_{}, terrainIndexed_{};
    mutable Vector2 terrainHighlight_{};
    mutable std::array<std::array<float,4>,6> terrainSamples_{};
    void finishTerrainLighting() const;
    int destinationLocation_ = -1, paletteLocation_ = -1, tableLocation_ = -1, indicesLocation_ = -1, opaqueLocation_ = -1;
    void drawSprite(const Sprite *, Rectangle, Texture2D) const;
    int paletteIndex(Color) const;

  public:
    // Scope only consecutive terrain entries. End before units, unlit draws,
    // background capture or any other consumer of the graphics state.
    class TerrainBatch {
        const PaletteBlendView &owner_;
      public:
        explicit TerrainBatch(const PaletteBlendView &);
        ~TerrainBatch();
        TerrainBatch(const TerrainBatch &) = delete;
        TerrainBatch &operator=(const TerrainBatch &) = delete;
    };
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
    void setLighting(Texture2D lightMap, Vec player, Vec playerScreen, Vec origin, float zoom, Color ambient,
                     bool fullBright = false);
    // Empty samples: floor Gouraud field; one: unit anchor; six: native DT1 wall columns.
    // Transform the source before alpha/additive composition, never the completed frame.
    void drawLit(const Sprite *, Vec position, std::span<const Color> samples, Color tint = WHITE,
                 Vector2 highlight = {1, 1}, bool screenBlend = false) const;
    void drawWall(const Sprite *, Vec position, std::span<const Color> samples, uint8_t alpha) const;
};
} // namespace d2x
