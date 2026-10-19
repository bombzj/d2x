#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace d2x {
struct MapTerrain;
struct Vec;
// Client-only TileData+24 / red-green-blue / deadline state. Preset pops and
// foreground walls share the original transition, independent of collision.
class TileVisibility {
    using Parent = std::tuple<int,int,int,int,int>;
    using Key = std::tuple<int,int,int,int,int,int,int,uint32_t>;
    struct State {
        int red{255}, from{255}, to{255};
        int64_t deadline{};
        uint8_t flags{};
        bool hidden{};
        std::optional<bool> popHidden;
    };
    struct Draw { uint8_t alpha{255}; bool alternateLight{}; };
    std::map<Key,State> tiles_;
    std::map<std::pair<Parent,int>,int64_t> hiddenGroups_;
    std::vector<Draw> draw_;
    static void advance(State &, int64_t now);
    static void transition(State &, int from, int to, int64_t deadline);
  public:
    void clear();
    void update(const MapTerrain &, Vec position, int originX, int originY, double seconds);
    uint8_t alpha(size_t tile) const { return draw_.at(tile).alpha; }
    bool alternateLight(size_t tile) const { return draw_.at(tile).alternateLight; }
};
} // namespace d2x
