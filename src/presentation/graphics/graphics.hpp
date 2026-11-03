#pragma once
#include "resources/archive.hpp"
#include "resources/formats.hpp"
#include "core/math.hpp"
#include <optional>
#include <functional>
#include <memory>
#include <raylib.h>
#include <unordered_map>
#include <vector>
namespace d2x {
class WorldArt;
// The page owns the GPU allocation. A recycled page invalidates its previous stamp.
struct AtlasCell {
    Texture2D page{};
    Rectangle rect{};
    std::weak_ptr<uint64_t> stamp;
    bool touch() const;
};
struct SpriteLayer {
    Texture2D texture{};
    int x = 0, y = 0;
    bool softAdditive = false;
    Texture2D indexedTexture{};
    AtlasCell cell;
    // COF transparency enabled: native CelDraw mode; otherwise NORMAL (5).
    uint8_t drawMode = 5;
};
struct SpriteComponentBounds {
    Rectangle bounds{};
    bool selectable = false;
    int component = -1;
};
struct Sprite {
    Texture2D texture{};
    int x = 0, y = 0;
    // Opaque extents remain available for font ink/layout measurements.
    int hitX = 0, hitY = 0, hitWidth = 0, hitHeight = 0;
    Texture2D shadowTexture{};
    int shadowX = 0, shadowY = 0;
    std::vector<SpriteLayer> layers;
    // Original DCC/DC6 indices for PL2 blending; zero remains transparent.
    Texture2D indexedTexture{};
    // Native selection uses original frame extents, including transparent pixels.
    std::optional<Rectangle> selectionFrameBounds;
    std::optional<Rectangle> selectionCoarseBounds;
    std::vector<SpriteComponentBounds> componentBounds;
    AtlasCell cell, colorCell, shadowCell;
    std::shared_ptr<const std::array<uint8_t, 256>> translation;
    std::optional<Vec> riderAnchor;
    bool touch() const;
};
struct GpuAnimation {
    int directions = 0, count = 0;
    std::vector<Sprite> frames;
    bool completeComposite = false;
    struct Source {
        std::function<const Sprite *(size_t)> resolve;
    };
    struct Sample { std::shared_ptr<const Source> source; size_t offset; };
    std::shared_ptr<const Source> source;
    std::vector<Sample> sequence;
    std::optional<Rectangle> bounds;
    bool intersects(Vec position, Rectangle viewport) const;
    bool available() const { return source || !sequence.empty() || !frames.empty(); }
    void clearFrames() { frames.clear(); sequence.clear(); source.reset(); bounds.reset(); }
    void appendSample(const GpuAnimation &from, int facing, int index);
    const Sprite *frame(int direction, int index) const;

};
enum class WorldSingleArt { Object, Effect, BlendedEffect, ItemIcon, GroundItem, Overlay, BlendedOverlay };
class Graphics {
    enum class AssetKind { Direct, Single, Composite };
    std::array<uint64_t, 3> textureCounts{}, textureBytes{};
    Sprite uploadAsset(const IndexedFrame &frame, bool translucent, AssetKind kind);
    Palette palette;
    std::vector<Texture2D> textures;
    std::unordered_map<uint64_t, Sprite> textureCache;
    std::map<std::string, std::shared_ptr<const Animation>> decoded;
    std::vector<std::weak_ptr<const Animation>> decodedSources;
    std::shared_ptr<const Animation> animationSource(const std::string &path);
    Archives &archives;
    std::unique_ptr<WorldArt> world_;
    WorldArt &worldArt();

  public:
    explicit Graphics(Archives &archives,
                      const std::string &palettePath = "data/global/palette/act1/pal.dat");
    ~Graphics();
    Graphics(const Graphics &) = delete;
    Graphics &operator=(const Graphics &) = delete;
    Sprite upload(const IndexedFrame &frame, bool translucent = false);
    const Animation *animation(const std::string &path);
    GpuAnimation single(const std::string &path, bool translucent = false);
    Sprite terrain(const IndexedFrame &);
    GpuAnimation worldSingle(const std::string &path, WorldSingleArt kind = WorldSingleArt::Object, const uint8_t *itemShift = nullptr);
    GpuAnimation composite(const std::string &type, const std::string &token, const std::string &mode,
                           const std::string &weapon,
                           const std::array<const char *, 16> *equipment = nullptr,
                           const std::array<uint8_t, 256> *colorMap = nullptr, bool menuPortrait = false);
    void releaseDecoded() { decoded.clear(); }
};
} // namespace d2x
