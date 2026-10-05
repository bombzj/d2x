#include "remote_scene.hpp"
#include "content/character/realm_portrait.hpp"
#include "content/monsters/monster_animation.hpp"
#include "network/protocol/bits.hpp"
#include "presentation/hud/hud_layout.hpp"
#include "presentation/world/scene_geometry.hpp"
#include "resources/anim_data.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <bit>
#include <cctype>
#include <sstream>

namespace d2x {
namespace {
constexpr std::array componentCodes{"HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
                                    "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8"};
std::string lower(std::string value) {
    for (auto &ch : value)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
std::vector<std::string> variants(std::string_view value) {
    std::string text(value);
    std::erase(text, '"');
    std::istringstream stream(text);
    std::vector<std::string> result;
    for (std::string part; std::getline(stream, part, ',');) {
        part.erase(0, part.find_first_not_of(" \t"));
        const auto end = part.find_last_not_of(" \t");
        if (end != std::string::npos)
            part.resize(end + 1);
        if (!part.empty())
            result.push_back(lower(part));
    }
    return result;
}
ClassicFont font(Graphics &g, Archives &a) {
    ClassicFont f;
    const std::string base = "data/local/font/latin/font16";
    f.glyphs = g.single(base + ".dc6");
    const auto table = a.read(base + ".tbl");
    if (table.size() < 3596 || f.glyphs.frames.empty())
        throw std::runtime_error("Original scene font is unavailable");
    for (int i = 0; i < 256; ++i) {
        f.widths[size_t(i)] = table[size_t(12 + i * 14 + 3)];
        f.indices[size_t(i)] = table[size_t(12 + i * 14 + 8)];
    }
    f.ready = true;
    return f;
}
void imageAt(const Sprite *s, Rectangle bounds, Color tint = WHITE) {
    if (s && s->texture.id)
        DrawTexturePro(s->texture, {0, 0, float(s->texture.width), float(s->texture.height)}, bounds, {0, 0},
                       0, tint);
}
bool visible(const Sprite &s, Vec p) {
    return p.x + s.x < W && p.y + s.y < H - HUD && p.x + s.x + s.texture.width > 0 &&
           p.y + s.y + s.texture.height > 0;
}
} // namespace
struct RemoteScene::Impl {
    struct Art {
        GpuAnimation animation;
        float fps{};
        int start{};
        bool cycle{}, shadow{};
        Vec offset;
        int order{};
    };
    struct Motion {
        std::optional<OnlinePoint> last;
        Vec look;
        float movedAt{-1};
    };
    Archives &archives;
    Graphics terrain, actors, units, ui;
    RealmPortraitCatalog portraits;
    AnimDataTable animations;
    DataTable objects, monstats, monstats2;
    std::map<int, size_t> objectRows, monsterRows;
    std::map<std::string, size_t, std::less<>> monsterExtra;
    std::map<std::string, Art> art;
    std::map<OnlineUnitKey, Motion> motion;
    std::vector<Sprite> tiles;
    GpuAnimation panel, cursor, exit, resume;
    ClassicFont normal;
    const Map *currentMap{};
    uint64_t gameGeneration{~uint64_t{}}, areaGeneration{~uint64_t{}};
    float time{};
    bool menu{}, running{true};
    int rendered{}, unavailable{};
    bool playerDisplayed{};
    Impl(Archives &a)
        : archives(a), terrain(a), actors(a), units(a, "data/global/palette/units/pal.dat"),
          ui(a, "data/global/palette/sky/pal.dat"), portraits(a),
          animations(a.read("data/global/animdata.d2")), objects(a.read("data/global/excel/objects.txt")),
          monstats(a.read("data/global/excel/monstats.txt")),
          monstats2(a.read("data/global/excel/monstats2.txt")), normal(font(ui, a)) {
        for (size_t row = 0; row < objects.rows().size(); ++row)
            if (auto id = objects.number(row, "Id"))
                objectRows.emplace(*id, row);
        for (size_t row = 0; row < monstats.rows().size(); ++row)
            if (auto id = monstats.number(row, "hcIdx"))
                monsterRows.emplace(*id, row);
        for (size_t row = 0; row < monstats2.rows().size(); ++row) {
            const auto id = monstats2.value(row, "Id");
            if (!id.empty())
                monsterExtra.emplace(std::string(id), row);
        }
        panel = ui.single("data/global/ui/panel/800ctrlpnl7.dc6");
        cursor = units.single("data/global/ui/cursor/ohand.dc6");
        exit = units.single("data/local/ui/eng/exit.dc6");
        resume = units.single("data/local/ui/eng/returntogame.dc6");
        // Fonts/cursor/panel are mandatory original assets, never generated substitutes.
        if (panel.frames.size() < 6 || cursor.frames.empty() || exit.frames.empty() || resume.frames.empty())
            throw std::runtime_error("Original online scene controls are unavailable");
    }
    Art *composite(const std::string &category, const RealmPortraitParts &parts, const std::string &mode,
                   bool shadow) {
        std::string key = category + ":" + parts.token + mode + parts.weapon;
        for (const auto &part : parts.components)
            key += ":" + part;
        if (auto found = art.find(key); found != art.end())
            return &found->second;
        auto [entry, inserted] = art.try_emplace(key);
        (void)inserted;
        auto &result = entry->second;
        const auto base = "data/global/" + category + "/" + parts.token + "/";
        const auto cofBytes =
            archives.read(base + "cof/" + parts.token + mode + parts.weapon + ".cof", false);
        if (cofBytes.empty())
            return &result;
        const auto cof = decodeCof(cofBytes);
        for (size_t layer = 0; layer < cof.components.size(); ++layer) {
            const auto c = size_t(cof.components[layer]);
            if (c >= parts.components.size() || parts.components[c].empty())
                return &result;
            if (parts.components[c] == "nil")
                continue;
            const auto component = lower(componentCodes[c]);
            const auto path = base + component + "/" + parts.token + component + parts.components[c] + mode +
                              cof.weapons[layer];
            // Prevent Graphics' legacy non-character fallback from substituting a weapon.
            if (!archives.contains(path + ".dcc") && !archives.contains(path + ".dc6"))
                return &result;
        }
        std::array<const char *, 16> pointers;
        for (size_t c = 0; c < pointers.size(); ++c)
            pointers[c] = parts.components[c].c_str();
        result.animation = actors.composite(category, parts.token, mode, parts.weapon, &pointers);
        if (!result.animation.completeComposite)
            result.animation = {};
        result.shadow = shadow;
        std::string animKey = parts.token + mode + parts.weapon;
        for (auto &ch : animKey)
            ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (const auto *record = animations.find(animKey); record && record->speed > 0)
            result.fps = float(record->speed) * 25 / 256;
        result.cycle = mode == "nu" || mode == "tn" || mode == "wl" || mode == "tw" || mode == "rn";
        return &result;
    }
    Art *character(const OnlineUnit &u, bool moving) {
        const auto parts = portraits.decode(u, world());
        if (!parts)
            return nullptr;
        std::string mode;
        if (u.mode == 8)
            mode = "dt";
        else if (moving)
            mode = "tw"; // Server-confirmed movement within town.
        else if (!u.mode || *u.mode == 7 || *u.mode == 1 || *u.mode == 0 || *u.mode == 0x17)
            mode = "tn";
        else
            return nullptr;
        return composite("chars", *parts, mode, true);
    }
    const OnlineWorldView *worldView{};
    const OnlineWorldView &world() const { return *worldView; }
    Art *monster(const OnlineUnit &u, bool moving) {
        if (!u.classId)
            return nullptr;
        const auto row = monsterRows.find(*u.classId);
        if (row == monsterRows.end())
            return nullptr;
        const auto extra = monsterExtra.find(monstats.value(row->second, "MonStatsEx"));
        if (extra == monsterExtra.end())
            return nullptr;
        const auto e = extra->second;
        RealmPortraitParts parts;
        parts.token = lower(std::string(monstats.value(row->second, "Code")));
        if (parts.token.empty())
            return nullptr;
        try {
            net::protocol::BitReader bits(u.appearanceBits);
            const auto mode = bits.read(4);
            const bool components = bits.read(1) != 0;
            for (size_t c = 0; c < parts.components.size(); ++c) {
                auto values = variants(monstats2.value(e, std::string(componentCodes[c]) + "v"));
                const auto selected =
                    components
                        ? bits.read(values.size() >= 3 ? std::bit_width(unsigned(values.size() - 1)) : 1)
                        : 0;
                if (!monstats2.number(e, componentCodes[c]).value_or(0)) {
                    parts.components[c] = "nil";
                    continue;
                }
                if (values.empty() || selected >= values.size() || (!components && values.size() > 1))
                    return nullptr;
                parts.components[c] = values[selected];
            }
            // Unique/mercenary color and state effects require another decoder.
            if (bits.read(1))
                return nullptr;
            const auto actual = u.mode.value_or(uint8_t(mode));
            constexpr std::array modes{"dt", "nu", "wl", "gh", "a1", "a2", "bl", "sc",
                                       "s1", "s2", "s3", "s4", "dd", "kb", "sq", "rn"};
            if (actual >= modes.size())
                return nullptr;
            const std::string pose = moving ? "wl" : modes[actual];
            parts.weapon = monsterModeWeapon(archives, parts.token, pose, monstats2.value(e, "BaseW"));
            if (parts.weapon.empty())
                return nullptr;
            return composite("monsters", parts, pose, monstats2.number(e, "Shadow").value_or(0) != 0);
        } catch (const net::protocol::ProtocolError &) {
            return nullptr;
        }
    }
    Art *object(const OnlineUnit &u) {
        if (!u.classId || !u.mode || *u.mode >= 8)
            return nullptr;
        const auto found = objectRows.find(*u.classId);
        if (found == objectRows.end() || !objects.number(found->second, "Draw").value_or(0))
            return nullptr;
        const auto row = found->second;
        constexpr std::array modes{"nu", "op", "on", "s1", "s2", "s3", "s4", "s5"};
        const std::string suffix = std::to_string(*u.mode);
        if (!objects.number(row, "Mode" + suffix).value_or(0))
            return nullptr;
        const auto token = lower(std::string(objects.value(row, "Token")));
        const auto key = "object:" + std::to_string(*u.classId) + ":" + suffix;
        if (auto cached = art.find(key); cached != art.end())
            return &cached->second;
        Art result;
        const auto base = "data/global/objects/" + token + "/";
        auto path = base + "tr/" + token + "trlit" + modes[*u.mode] + "hth";
        if (archives.contains(path + ".dcc"))
            result.animation = actors.single(path + ".dcc");
        else if (archives.contains(path + ".dc6"))
            result.animation = actors.single(path + ".dc6");
        else {
            RealmPortraitParts parts;
            parts.token = token;
            parts.weapon = "hth";
            parts.components.fill("lit");
            if (auto *composed = composite("objects", parts, modes[*u.mode], false))
                result = *composed;
        }
        result.fps = float(objects.number(row, "FrameDelta" + suffix).value_or(0)) * 25 / 256;
        result.start = std::max(0, objects.number(row, "Start" + suffix).value_or(0));
        result.cycle = objects.number(row, "CycleAnim" + suffix).value_or(0) != 0;
        result.offset = {float(objects.number(row, "Xoffset").value_or(0)),
                         float(objects.number(row, "Yoffset").value_or(0))};
        result.order = objects.number(row, "DrawUnder").value_or(0)
                           ? 1
                           : objects.number(row, "OrderFlag" + suffix).value_or(0);
        return &art.emplace(key, std::move(result)).first->second;
    }
    RemoteSceneIntent draw(const OnlineView &v, const Map &map, const OnlineSceneView &binding, Vec mouse) {
        RemoteSceneIntent intent;
        worldView = &v.world;
        if (gameGeneration != v.gameGeneration || areaGeneration != v.world.areaGeneration) {
            gameGeneration = v.gameGeneration;
            areaGeneration = v.world.areaGeneration;
            motion.clear();
            menu = false;
            time = 0;
            currentMap = nullptr;
        }
        time += std::clamp(GetFrameTime(), 0.f, .1f);
        if (currentMap != &map) {
            currentMap = &map;
            tiles.clear();
            for (const auto *tile : map.terrain.tiles)
                tiles.push_back(terrain.upload(tile->image));
        }
        if (!binding.origin || !v.world.playerPosition)
            return intent;
        const auto origin = *binding.origin;
        auto local = [&](OnlinePoint p) {
            return Vec{float(int(p.x) - origin.x), float(int(p.y) - origin.y)};
        };
        const Vec camera = local(*v.world.playerPosition) + Vec{.5f, .5f};
        auto screen = [&](Vec p) { return project(p - camera) + Vec{W * .5f, (H - HUD) * .5f}; };
        struct Draw {
            SceneOrder order;
            const Sprite *image;
            Vec position;
            Color tint{WHITE};
            bool shadow{};
        };
        std::vector<Draw> draw, roofs;
        auto addTile = [&](MapCell cell, int x, int y, int pass, int layer, bool wall, Color tint = WHITE) {
            if (!cell.present())
                return;
            const auto index = map.terrain.renderTileIndex(cell, x, y, time);
            if (index < 0 || size_t(index) >= tiles.size())
                return;
            const Vec feet{x * 5.f, y * 5.f}, p = screen(feet);
            if (!visible(tiles[size_t(index)], p))
                return;
            Draw item{sceneOrder(feet, pass, wall, layer), &tiles[size_t(index)], p, tint, false};
            if (cell.orientation == 15) {
                for (const auto &popup : map.terrain.data.roofPopups)
                    if (popup.contains(camera) && popup.covers(x, y, map.terrain.tiles[size_t(index)]->main))
                        item.tint.a = 0;
                roofs.push_back(item);
            } else
                draw.push_back(item);
        };
        const auto &data = map.terrain.data;
        for (int y = 0; y < data.height; ++y)
            for (int x = 0; x < data.width; ++x) {
                const auto cell = size_t(y) * data.width + x;
                for (const auto &floor : data.floors)
                    addTile(floor[cell], x, y, 0, 1, false);
                addTile(data.shadows[cell], x, y, 0, 2, false, {20, 22, 25, 100});
                for (size_t layer = 0; layer < data.walls.size(); ++layer) {
                    auto wall = data.walls[layer][cell];
                    const bool lowerWall = wall.orientation >= 16 && wall.orientation <= 19;
                    addTile(wall, x, y, lowerWall ? 0 : 1, lowerWall ? 0 : -100 + int(layer) * 2, !lowerWall);
                    if (wall.orientation == 3) {
                        wall.orientation = 4;
                        addTile(wall, x, y, 1, -99 + int(layer) * 2, true);
                    }
                }
            }
        rendered = unavailable = 0;
        playerDisplayed = false;
        std::erase_if(motion, [&](const auto &entry) { return !v.world.units.contains(entry.first); });
        for (const auto &[key, u] : v.world.units) {
            if (!u.position || key.type > 2)
                continue;
            Vec feet = local(*u.position);
            if (feet.x < 0 || feet.y < 0 || feet.x >= binding.width || feet.y >= binding.height)
                continue;
            if (!u.classId) {
                ++unavailable;
                continue;
            }
            auto &m = motion[key];
            if (m.last && *m.last != *u.position) {
                m.look = local(*u.position) - local(*m.last);
                m.movedAt = time;
            }
            m.last = u.position;
            if (u.destination)
                m.look = local(*u.destination) - feet;
            const bool moving = m.movedAt >= 0 && time - m.movedAt < .2f;
            Art *visual = key.type == 0   ? character(u, moving)
                          : key.type == 1 ? monster(u, moving)
                                          : object(u);
            if (!visual || visual->animation.frames.empty()) {
                ++unavailable;
                continue;
            }
            if (key.type != 2)
                feet = feet + Vec{.5f, .5f};
            const auto &animation = visual->animation;
            const int advance = int(time * std::max(0.f, visual->fps));
            const int index = visual->cycle ? (visual->start + advance) % std::max(1, animation.count)
                                            : std::min(visual->start + advance, animation.count - 1);
            const auto *image = animation.frame(direction(m.look, animation.directions), std::max(0, index));
            const auto p = screen(feet) + visual->offset;
            if (!image || !visible(*image, p))
                continue;
            ++rendered;
            if (key.type == 0 && v.load.playerUnitId == key.id)
                playerDisplayed = true;
            draw.push_back({sceneOrder(feet, visual->order == 1 ? 0 : 1, visual->order == 2, 2), image, p,
                            WHITE, visual->shadow});
        }
        auto paint = [&](auto &list) {
            std::stable_sort(list.begin(), list.end(),
                             [](const auto &a, const auto &b) { return a.order < b.order; });
            for (const auto &item : list) {
                if (item.shadow)
                    spriteShadow(item.image, item.position);
                sprite(item.image, item.position, item.tint);
            }
        };
        BeginScissorMode(0, 0, W, H - HUD);
        paint(draw);
        paint(roofs);
        EndScissorMode();
        DrawRectangle(0, H - HUD, W, HUD, BLACK);
        constexpr std::array<float, 6> offsets{0, 165, 293, 421, 549, 683};
        for (size_t i = 0; i < offsets.size(); ++i) {
            const auto &part = panel.frames[i];
            imageAt(&part, hudRect(offsets[i], float(part.texture.height), float(part.texture.width),
                                   float(part.texture.height)));
        }
        UiPainter text(normal);
        if (v.world.life)
            text.label("Life: " + std::to_string(*v.world.life), 10, 10, 16);
        if (v.world.mana)
            text.label("Mana: " + std::to_string(*v.world.mana), 10, 30, 16);
        if (unavailable)
            text.label("Unavailable unit art: " + std::to_string(unavailable), 10, 50, 16);
        if (!playerDisplayed)
            text.label("Waiting for supported server player appearance", 10, 70, 16);
        if (IsKeyPressed(KEY_R))
            running = !running;
        if (IsKeyPressed(KEY_ESCAPE))
            menu = !menu;
        if (menu) {
            auto button = [&](const GpuAnimation &label, float y) {
                const auto *image = label.frame(0, 0);
                if (!image)
                    return false;
                const Vec p{W * .5f - image->texture.width * .5f - image->x, y};
                sprite(image, p);
                return IsWindowFocused() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                       CheckCollisionPointRec(rv(mouse),
                                              {p.x + image->x, p.y + image->y, float(image->texture.width),
                                               float(image->texture.height)});
            };
            if (button(resume, 250) || IsKeyPressed(KEY_ENTER))
                menu = false;
            if (button(exit, 340))
                intent.leave = true;
        } else if (binding.movementAvailable && IsWindowFocused() && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                   mouse.x >= 0 && mouse.x < W && mouse.y >= 0 && mouse.y < H - HUD && !hudSurface(mouse)) {
            const auto target = unproject(mouse - Vec{W * .5f, (H - HUD) * .5f}) + camera;
            const int x = int(std::floor(target.x)) + origin.x, y = int(std::floor(target.y)) + origin.y;
            if (x >= origin.x && y >= origin.y && x < origin.x + binding.width &&
                y < origin.y + binding.height)
                intent.move = OnlinePoint{uint16_t(x), uint16_t(y)};
            intent.run = running;
        }
        cursorSprite(cursor.frame(0, 0), mouse, handCursorHotspot(cursor.frame(0, 0)));
        return intent;
    }
};
RemoteScene::RemoteScene(Archives &a) : impl_(std::make_unique<Impl>(a)) {}
RemoteScene::~RemoteScene() = default;
RemoteSceneIntent RemoteScene::frame(const OnlineView &v, const Map &m, const OnlineSceneView &s, Vec mouse) {
    return impl_->draw(v, m, s, mouse);
}
int RemoteScene::renderedUnits() const {
    return impl_->rendered;
}
int RemoteScene::unavailableUnits() const {
    return impl_->unavailable;
}
bool RemoteScene::playerDisplayed() const {
    return impl_->playerDisplayed;
}
} // namespace d2x
