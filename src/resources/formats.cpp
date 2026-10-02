#include "formats.hpp"
#include "path.hpp"
#include <algorithm>
#include <sstream>
namespace d2x {
Palette decodePalette(const Bytes &b) {
    if (b.size() < 768)
        throw std::runtime_error("Invalid palette");
    Palette p;
    for (int i = 0; i < 256; i++)
        p[i] = {b[i * 3 + 2], b[i * 3 + 1], b[i * 3], uint8_t(i ? 255 : 0)};
    return p;
}
Animation decodeDc6(const Bytes &b) {
    Reader r(b);
    if (r.u32() != 6)
        throw std::runtime_error("Unsupported DC6 version");
    r.skip(12);
    Animation a;
    a.directions = r.u32();
    a.framesPerDirection = r.u32();
    if (a.directions < 1 || a.directions > 32 || a.framesPerDirection < 1 || a.framesPerDirection > 4096)
        throw std::runtime_error("Invalid DC6 frames");
    std::vector<uint32_t> offsets(a.directions * a.framesPerDirection);
    for (auto &o : offsets)
        o = r.u32();
    for (auto o : offsets) {
        r.seek(o);
        bool flip = r.u32() != 0;
        IndexedFrame f;
        f.width = r.u32();
        f.height = r.u32();
        f.x = r.i32();
        f.y = r.i32();
        r.skip(8);
        auto len = r.u32();
        if (f.width < 1 || f.height < 1 || f.width > 4096 || f.height > 4096)
            throw std::runtime_error("Invalid DC6 dimensions");
        r.need(len);
        auto end = r.pos + len;
        f.pixels.resize(size_t(f.width) * f.height);
        int x = 0, y = flip ? 0 : f.height - 1;
        while (r.pos < end) {
            auto c = r.u8();
            if (c == 0x80) {
                x = 0;
                y += flip ? 1 : -1;
            } else if (c & 0x80)
                x += c & 127;
            else {
                if (r.pos + c > end || y < 0 || y >= f.height || x + c > f.width)
                    throw std::runtime_error("Invalid DC6 run");
                for (int n = 0; n < c; n++)
                    f.pixels[y * f.width + x++] = r.u8();
            }
        }
        a.frames.push_back(std::move(f));
    }
    return a;
}
Cof decodeCof(const Bytes &b) {
    Reader r(b);
    Cof c;
    c.layers = r.u8();
    c.frames = r.u8();
    c.directions = r.u8();
    r.skip(25);
    if (c.layers < 1 || c.layers > 16 || c.frames < 1 || c.directions < 1 || c.directions > 32)
        throw std::runtime_error("Invalid COF");
    for (int i = 0; i < c.layers; i++) {
        c.components.push_back(r.u8());
        c.shadows.push_back(r.u8() != 0);
        r.skip(1); // Selectable.
        c.transparent.push_back(r.u8() != 0);
        c.drawEffects.push_back(r.u8());
        std::string w;
        for (int j = 0; j < 4; j++) {
            auto ch = r.u8();
            if (ch)
                w += char(ch);
        }
        c.weapons.push_back(w);
    }
    r.skip(c.frames);
    r.need(size_t(c.frames) * c.directions * c.layers);
    c.order.assign(b.begin() + r.pos, b.begin() + r.pos + size_t(c.frames) * c.directions * c.layers);
    return c;
}
int Cof::componentAt(int d, int f, int l) const {
    return order[((d % directions) * frames + (f % frames)) * layers + l];
}
std::vector<Tile> decodeDt1(const Bytes &b) {
    Reader r(b);
    if (r.u32() != 7 || r.u32() != 6)
        throw std::runtime_error("Unsupported DT1 version");
    r.seek(268);
    auto count = r.u32(), offset = r.u32();
    if (count > 32768)
        throw std::runtime_error("Invalid DT1 count");
    std::vector<Tile> result;
    for (uint32_t i = 0; i < count; i++) {
        r.seek(offset + size_t(i) * 96);
        r.skip(4);
        int roofHeight = r.i16();
        r.skip(2);
        int h = r.i32(), w = r.i32();
        r.skip(4);
        Tile t;
        t.roofHeight = roofHeight;
        t.orientation = r.u32();
        t.main = r.u32();
        t.sub = r.u32();
        t.rarity = r.u32();
        r.skip(4);
        for (auto &flag : t.flags)
            flag = r.u8();
        r.skip(7);
        auto bp = r.u32();
        r.skip(4);
        auto nb = r.u32();
        if (nb > 8192)
            throw std::runtime_error("Invalid DT1 blocks");
        if (w <= 0 || h == 0 || nb == 0) {
            result.push_back(std::move(t));
            continue;
        }
        struct Block {
            int x, y, format;
            uint32_t len, off;
        };
        std::vector<Block> blocks;
        int minY = 0, maxY = 0, maxX = w;
        for (uint32_t j = 0; j < nb; j++) {
            r.seek(bp + size_t(j) * 20);
            Block z;
            z.x = r.i16();
            z.y = r.i16();
            r.skip(4);
            z.format = r.u16();
            z.len = r.u32();
            r.skip(2);
            z.off = r.u32();
            minY = std::min(minY, z.y);
            maxY = std::max(maxY, z.y + 32);
            maxX = std::max(maxX, z.x + 32);
            blocks.push_back(z);
        }
        if (t.orientation == 0 || t.orientation == 15) {
            minY = 0;
            maxY = 80;
        }
        if (maxX > 4096 || maxY - minY > 4096 || maxX <= 0)
            throw std::runtime_error("Invalid DT1 dimensions");
        t.image.width = maxX;
        t.image.height = maxY - minY;
        t.image.x = -80;
        // DT1 wall coordinates end at y=0; their origin is the front corner of the floor.
        t.image.y = t.orientation == 0 ? 0 : t.orientation == 15 ? -t.roofHeight : minY + 80;
        t.image.pixels.resize(size_t(maxX) * (maxY - minY));
        auto put = [&](int x, int y, uint8_t v) {
            y -= minY;
            if (x >= 0 && x < maxX && y >= 0 && y < t.image.height)
                t.image.pixels[y * maxX + x] = v;
        };
        for (auto &z : blocks) {
            r.seek(size_t(bp) + z.off);
            r.need(z.len);
            auto end = r.pos + z.len;
            if (z.format == 1) {
                if (z.len != 256)
                    throw std::runtime_error("Invalid DT1 diamond");
                static constexpr int starts[] = {14, 12, 10, 8, 6, 4, 2, 0, 2, 4, 6, 8, 10, 12, 14};
                for (int y = 0; y < 15; y++)
                    for (int x = starts[y]; x < 32 - starts[y]; x++)
                        put(z.x + x, z.y + y, r.u8());
            } else {
                int x = 0, y = 0;
                while (r.pos < end) {
                    if (end - r.pos < 2)
                        throw std::runtime_error("Invalid DT1 RLE");
                    auto skip = r.u8(), n = r.u8();
                    if (!skip && !n) {
                        x = 0;
                        y++;
                    } else {
                        x += skip;
                        if (n > end - r.pos || x + n > 4096 || y > 4096)
                            throw std::runtime_error("Invalid DT1 run");
                        for (int k = 0; k < n; k++)
                            put(z.x + x++, z.y + y, r.u8());
                    }
                }
            }
        }
        result.push_back(std::move(t));
    }
    return result;
}
MapData decodeDs1(const Bytes &b) {
    Reader r(b);
    MapData m;
    m.version = r.u32();
    m.width = r.u32() + 1;
    m.height = r.u32() + 1;
    if (m.version < 4 || m.version > 18 || m.width < 1 || m.width > 512 || m.height < 1 || m.height > 512)
        throw std::runtime_error("Unsupported DS1 header");
    if (m.version >= 8)
        m.act = std::min(int(r.u32()), 4);
    int tags = m.version >= 10 ? r.u32() : 0;
    m.substitutionMethod = tags;
    int files = r.u32();
    if (files < 0 || files > 256)
        throw std::runtime_error("Invalid DS1 dependencies");
    for (int i = 0; i < files; i++) {
        auto s = normalize(r.str());
        auto p = s.find("\\tiles\\");
        if (p != std::string::npos)
            s = "data\\global" + s.substr(p);
        m.dependencies.push_back(s);
    }
    if (m.version >= 9 && m.version <= 13)
        r.skip(8);
    int walls = r.u32(), floors = m.version >= 16 ? r.u32() : 1;
    if (walls < 0 || walls > 4 || floors < 0 || floors > 2 || (!walls && !floors))
        throw std::runtime_error("Invalid DS1 layers");
    auto n = size_t(m.width) * m.height;
    static constexpr int dirs[] = {0, 1, 2,  1,  2,  3,  3,  5,  5,  6,  6,  7, 7,
                                   8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20};
    for (int l = 0; l < walls; l++) {
        std::vector<MapCell> layer(n);
        for (auto &c : layer)
            c.value = r.u32();
        for (auto &c : layer) {
            c.orientation = r.u32();
            if (m.version < 7) {
                if (c.orientation < 0 || c.orientation >= 25)
                    throw std::runtime_error("Invalid DS1 orientation");
                c.orientation = dirs[c.orientation];
            }
        }
        m.walls.push_back(std::move(layer));
    }
    for (int l = 0; l < floors; l++) {
        std::vector<MapCell> layer(n);
        for (auto &c : layer)
            c.value = r.u32();
        m.floors.push_back(std::move(layer));
    }
    m.shadows.resize(n);
    for (auto &c : m.shadows) {
        c.value = r.u32();
        c.orientation = 13;
    }
    if (tags == 1 || tags == 2) {
        m.substitutions.resize(n);
        for (auto &tag : m.substitutions)
            tag = r.u32();
    }
    int objects = r.u32();
    if (objects < 0 || objects > 65536)
        throw std::runtime_error("Invalid DS1 objects");
    for (int i = 0; i < objects; i++) {
        MapObject o;
        o.type = r.u32();
        o.id = r.u32();
        o.x = r.u32();
        o.y = r.u32();
        if (m.version >= 6)
            o.flags = r.u32();
        m.objects.push_back(o);
    }
    if (m.version >= 12 && (tags == 1 || tags == 2)) {
        if (m.version >= 18)
            r.skip(4);
        int groups = r.u32();
        if (groups < 0 || groups > 65536)
            throw std::runtime_error("Invalid DS1 substitution group count");
        for (int index = 0; index < groups; ++index) {
            SubstitutionGroup group;
            group.x = r.u32();
            group.y = r.u32();
            group.width = r.u32();
            group.height = r.u32();
            if (m.version >= 13)
                group.variants = r.u32();
            if (group.x < 0 || group.y < 0 || group.width <= 0 || group.height <= 0 || group.variants < 0 ||
                int64_t(group.x) + group.width > m.width || int64_t(group.y) + group.height > m.height)
                throw std::runtime_error("Invalid DS1 substitution group bounds");
            m.substitutionGroups.push_back(group);
        }
    }
    if (m.version >= 14 && r.pos < b.size()) {
        int paths = r.u32();
        if (paths < 0 || paths > 65536)
            throw std::runtime_error("Invalid DS1 path count");
        for (int index = 0; index < paths; ++index) {
            int nodes = r.u32();
            int x = r.u32(), y = r.u32();
            if (nodes < 0 || nodes > 4096)
                throw std::runtime_error("Invalid DS1 path node count");
            auto object = std::find_if(m.objects.begin(), m.objects.end(),
                                       [&](const auto &entry) { return entry.x == x && entry.y == y; });
            for (int node = 0; node < nodes; ++node) {
                MapObject::PathNode point;
                point.x = r.u32();
                point.y = r.u32();
                point.action = m.version >= 15 ? r.u32() : 1;
                if (object != m.objects.end())
                    object->path.push_back(point);
            }
        }
    }
    // DS1 orientation 10 markers come in paired corners. Their main index
    // identifies the popup group and their sub index identifies the roof style.
    // Match each wall layer separately, as in Diablerie's LevelBuilder.
    for (const auto &layer : m.walls) {
        std::array<std::pair<int, int>, 7> starts{};
        std::array<bool, 7> found{};
        for (int y = 0; y < m.height; ++y)
            for (int x = 0; x < m.width; ++x) {
                const auto &cell = layer[size_t(y) * m.width + x];
                if (cell.orientation != 10)
                    continue;
                const int main = int((cell.value >> 20) & 63);
                const int group = main == 8 ? 0 : main == 9 ? 1 : main == 10 ? 2 :
                                  main == 12 ? 3 : main == 13 ? 4 : main == 16 ? 5 :
                                  main == 20 ? 6 : -1;
                if (group < 0)
                    continue;
                if (!found[size_t(group)]) {
                    starts[size_t(group)] = {x, y};
                    found[size_t(group)] = true;
                } else {
                    auto [firstX, firstY] = starts[size_t(group)];
                    if (x > firstX && y > firstY)
                        m.roofPopups.push_back({firstX, firstY, x - firstX, y - firstY,
                                                int((cell.value >> 8) & 255)});
                    found[size_t(group)] = false;
                }
            }
    }
    return m;
}
Table decodeTable(const Bytes &data) {
    std::istringstream input(std::string(data.begin(), data.end()));
    std::string line;
    Table rows;
    std::vector<std::string> fields;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> cells;
        size_t start = 0;
        for (size_t i = 0; i <= line.size(); i++)
            if (i == line.size() || line[i] == '\t') {
                cells.push_back(line.substr(start, i - start));
                start = i + 1;
            }
        if (fields.empty()) {
            fields = cells;
            continue;
        }
        std::map<std::string, std::string> row;
        for (size_t i = 0; i < std::min(cells.size(), fields.size()); i++)
            row[fields[i]] = cells[i];
        rows.push_back(std::move(row));
    }
    return rows;
}
} // namespace d2x
