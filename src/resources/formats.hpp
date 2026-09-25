#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/bytes.hpp"
#include "core/math.hpp"
namespace d2x {
struct Reader {
    std::span<const uint8_t> data;
    size_t pos = 0;
    explicit Reader(std::span<const uint8_t> bytes) : data(bytes) {}
    void need(size_t n) const {
        if (pos > data.size() || n > data.size() - pos)
            throw std::runtime_error("Truncated game resource");
    }
    void seek(size_t p) {
        if (p > data.size())
            throw std::runtime_error("Invalid resource offset");
        pos = p;
    }
    void skip(size_t n) {
        need(n);
        pos += n;
    }
    uint8_t u8() {
        need(1);
        return data[pos++];
    }
    uint16_t u16() {
        auto a = u8();
        return a | (uint16_t(u8()) << 8);
    }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() {
        auto a = u16();
        return a | (uint32_t(u16()) << 16);
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    std::string str() {
        std::string s;
        while (auto c = u8()) {
            if (s.size() > 4096)
                throw std::runtime_error("Resource string too long");
            s += char(c);
        }
        return s;
    }
};
struct Pixel {
    uint8_t r = 0, g = 0, b = 0, a = 0;
};
using Palette = std::array<Pixel, 256>;
Palette decodePalette(const Bytes &data);
struct IndexedFrame {
    int width = 0, height = 0, x = 0, y = 0;
    Bytes pixels;
};
struct Animation {
    int directions = 0, framesPerDirection = 0;
    std::vector<IndexedFrame> frames;
};
Animation decodeDcc(const Bytes &data);
Animation decodeDc6(const Bytes &data);
struct Cof {
    int layers = 0, frames = 0, directions = 0;
    std::vector<int> components;
    std::vector<bool> shadows, transparent;
    std::vector<std::string> weapons;
    Bytes order;
    int componentAt(int dir, int frame, int layer) const;
};
Cof decodeCof(const Bytes &data);
struct Tile {
    int orientation = 0, main = 0, sub = 0, rarity = 0, roofHeight = 0;
    std::array<uint8_t, 25> flags{};
    IndexedFrame image;
    uint32_t key() const { return (main << 16) | (sub << 8) | orientation; }
};
std::vector<Tile> decodeDt1(const Bytes &data);
struct MapCell {
    uint32_t value = 0;
    int orientation = 0;
    size_t libraryScope = 0;
    uint32_t key() const { return (((value >> 20) & 63) << 16) | (((value >> 8) & 255) << 8) | orientation; }
    bool occupied() const {
        return (value & (orientation == 0 ? 2u : orientation == 13 ? 0x8000000u : 1u)) != 0;
    }
    bool hidden() const {
        return (value & 0x80000000u) != 0 ||
               (orientation == 0 && ((value >> 20) & 63) == 30 && ((value >> 8) & 255) <= 1);
    }
    bool present() const {
        return occupied() && !hidden() &&
               !((orientation == 10 || orientation == 11) && ((value >> 20) & 63) >= 8);
    }
};
struct MapObject {
    int type = 0, id = 0, x = 0, y = 0;
    uint32_t flags = 0;
    struct PathNode {
        int x = 0, y = 0, action = 1;
    };
    std::vector<PathNode> path;
};
struct SubstitutionGroup {
    int x = 0, y = 0, width = 0, height = 0, variants = 0;
};
struct RoofPopup {
    int x = 0, y = 0, width = 0, height = 0, roofMain = 0;
    bool contains(Vec player) const {
        return player.x >= x * 5 && player.y >= y * 5 &&
               player.x < (x + width) * 5 && player.y < (y + height) * 5;
    }
    bool covers(int tileX, int tileY, int main) const {
        return main == roofMain && tileX >= x - 1 && tileY >= y - 1 &&
               tileX < x + width + 2 && tileY < y + height + 2;
    }
};
struct MapData {
    int version = 0, width = 0, height = 0, act = 0;
    int substitutionMethod = 0;
    std::vector<std::string> dependencies;
    std::vector<std::vector<MapCell>> floors, walls;
    std::vector<MapCell> shadows;
    std::vector<uint32_t> substitutions;
    std::vector<SubstitutionGroup> substitutionGroups;
    std::vector<RoofPopup> roofPopups;
    std::vector<MapObject> objects;
};
MapData decodeDs1(const Bytes &data);
using Table = std::vector<std::map<std::string, std::string>>;
Table decodeTable(const Bytes &data);
} // namespace d2x
