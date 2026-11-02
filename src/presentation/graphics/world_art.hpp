#pragma once
#include "graphics.hpp"
#include "graphics_diagnostics.hpp"
#include <list>
namespace d2x {
// Render-thread cache. Display and selection geometry always use the original cell coordinates.
class WorldArt {
  public:
    struct Asset {
        std::string path;
        int directions{}, count{};
        bool dc6{};
        uint64_t id{};
        Rectangle bounds{}, compositeBounds{};
    };
    using AssetPtr = std::shared_ptr<const Asset>;
    enum class Use { Terrain, Actor, Effect, Item };
    explicit WorldArt(Archives &archives);
    void retireColdPages();
    std::map<std::string, GpuAnimation> recipes;
    ~WorldArt();
    AssetPtr asset(const std::string &path);
    std::shared_ptr<const IndexedFrame> pixels(const Asset &, int direction, int frame);
    GpuAnimation animation(int directions, int count, std::function<Sprite(size_t)> load);
    AtlasCell upload(const IndexedFrame &, int format, const void *pixels, Use use);
    Sprite component(const Asset &, int direction, int frame, const IndexedFrame &, Use use = Use::Actor);
    GraphicsDiagnostics usage() const;

  private:
    struct Page {
        Texture2D texture{};
        Use use;
        std::shared_ptr<uint64_t> stamp;
        int x{}, y{}, row{};
        size_t bytes{};
    };
    using Key = std::pair<uint64_t, size_t>;
    struct Decoded {
        Key key;
        std::shared_ptr<Animation> animation;
        size_t bytes;
    };
    struct Frame {
        Key key;
        Sprite sprite;
        uint64_t used;
    };
    Archives &archives_;
    std::map<std::string, AssetPtr, std::less<>> assets_;
    std::list<Decoded> decoded_;
    std::map<Key, std::list<Decoded>::iterator> decodedLookup_;
    std::list<Frame> frames_;
    std::map<Key, std::list<Frame>::iterator> frameLookup_;
    std::vector<Page> pages_;

    std::map<std::tuple<uint64_t, int, int>, AtlasCell> components_;
    size_t decodedBytes_{}, allocated_{}, peak_{};
    uint64_t nextId_{}, uploads_{}, uploadBytes_{}, evictions_{};
    const Sprite *resolve(Key key, const std::function<Sprite(size_t)> &load);
};
} // namespace d2x
