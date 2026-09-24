#pragma once
#include "resources/archive.hpp"
#include "resources/formats.hpp"
#include <raylib.h>
#include <unordered_map>
namespace d2x {
struct Sprite {
    Texture2D texture{};
    int x = 0, y = 0;
    int hitX = 0, hitY = 0, hitWidth = 0, hitHeight = 0;
};
struct GpuAnimation {
    int directions = 0, count = 0;
    std::vector<Sprite> frames;
    bool completeComposite = false;
    const Sprite *frame(int direction, int index) const {
        if (frames.empty() || directions <= 0 || count <= 0 || direction < 0 || index < 0)
            return nullptr;
        return &frames[(direction % directions) * count + (index % count)];
    }
};
class Graphics {
    Palette palette;
    std::vector<Texture2D> textures;
    std::unordered_map<uint64_t, Sprite> textureCache;
    std::map<std::string, Animation> decoded;
    Archives &archives;

  public:
    explicit Graphics(Archives &archives,
                      const std::string &palettePath = "data/global/palette/act1/pal.dat");
    ~Graphics();
    Graphics(const Graphics &) = delete;
    Graphics &operator=(const Graphics &) = delete;
    Sprite upload(const IndexedFrame &frame);
    const Animation *animation(const std::string &path);
    GpuAnimation single(const std::string &path);
    GpuAnimation composite(const std::string &type, const std::string &token, const std::string &mode,
                           const std::string &weapon,
                           const std::array<const char *, 16> *equipment = nullptr,
                           const std::array<uint8_t, 256> *colorMap = nullptr);
    void releaseDecoded() { decoded.clear(); }
};
} // namespace d2x
