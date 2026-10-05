#include "rooms.hpp"
#include <algorithm>
#include <stdexcept>

// D2MOO GenerateOutdoorLevel, BuildArea, AllocDrlgMap, PickSubThemes.
// MIT: docs/licenses/D2MOO.txt.
namespace d2x {
std::vector<RetailRoom> allocateRetailPresetRooms(const PresetRecord &preset, int file,
    int x, int y, int width, int height, uint32_t flags, Seed &random,
    const RetailPresetScanner &scan, bool singleRoom) {
    if (!scan || file < 0 || size_t(file) >= preset.variants.size() ||
        preset.variants[size_t(file)].empty() || width <= 0 || height <= 0)
        throw std::runtime_error("Invalid native preset area allocation inputs");
    if (preset.outdoors) flags |= 0x80000;
    const int gw = width / 8 + 1, gh = height / 8 + 1;
    const auto scanned = scan(preset, file, x, y, width, height, flags, random);
    if (scanned.size() != size_t(gw) * size_t(gh))
        throw std::runtime_error("Native preset scan returned invalid room flags");
    std::vector<RetailRoom> result;
    auto add = [&](int px, int py, int w, int h, uint32_t bits) {
        result.push_back({x + px, y + py, w, h, preset.id, file, x, y,
            bits | (preset.populate ? 0u : 0x800000u), 0, 0, preset.dt1Mask, 0,
            allocateRetailRoomSeed(random)});
    };
    if (singleRoom) {
        // Scanner coordinates are coalesced only for the native small-maze
        // single-room branch; this does not allocate intermediate children.
        uint32_t bits = 0;
        for (const auto value : scanned) bits |= value;
        add(0, 0, width, height, bits);
    } else for (int py = 0; py < height; py += 8)
        for (int px = 0; px < width; px += 8)
            add(px, py, std::min(8, width - px), std::min(8, height - py),
                scanned[size_t(py / 8) * size_t(gw) + size_t(px / 8)]);
    return result;
}
std::vector<RetailRoom> allocateRetailOutdoorRooms(const WorldCatalog &catalog,
    const NativeActLayout &act, int level, RetailOutdoorGrid &grid,
    const RetailPresetScanner &scan) {
    const auto &record = catalog.level(level);
    const auto &placement = act.levels.at(level);
    uint32_t baseMask = 0;
    switch (record.levelType) {
    case 2: baseMask = 0x44103; break;
    case 21: baseMask = 4; break;
    case 16: case 22: case 27: case 28: baseMask = 1; break;
    case 30: case 31: baseMask = 0x11; break;
    default: throw std::runtime_error("Unsupported native outdoor level type");
    }
    if (!scan) throw std::runtime_error("Native preset scanner is required");
    std::vector<RetailRoom> rooms;
    auto &random = grid.random();
    for (int y = 0; y < grid.height(); ++y)
        for (int x = 0; x < grid.width(); ++x) {
            const auto &cell = grid.cell(x, y);
            const int wx = placement.x + x * 8, wy = placement.y + y * 8;
            if (cell.flags & 0x200) {
                if (!cell.preset) continue; // Covered by another preset's footprint.
                const auto &preset = catalog.presets().at(cell.preset);
                // The subsequent forced-file override does not eliminate this draw.
                random.below(preset.files);
                const int file = int((cell.flags >> 16) & 15);
                if (preset.width <= 0 || preset.height <= 0)
                    throw std::runtime_error("Native outdoor preset has no declared extent");
                uint32_t flags = cell.links;
                const auto connection = act.connections.find(level);
                const auto slots = connection == act.connections.end()
                    ? NativeActLayout::ConnectionSlots{record.visible, record.warps}
                    : connection->second;
                for (int i = 0; i < 8; ++i)
                    if (slots.visible[size_t(i)] && slots.warps[size_t(i)] == -1)
                        flags |= 1u << (i + 4);
                auto children = allocateRetailPresetRooms(preset, file, wx, wy,
                    preset.width, preset.height, flags, random, scan);
                for (auto &room : children) rooms.push_back(std::move(room));
            } else if (!(cell.flags & 0x100)) {
                RetailRoom room{wx, wy, 8, 8, 0, 0, 0, 0, cell.links | 0x80000u,
                    cell.flags, cell.auxiliary, baseMask, 0, allocateRetailRoomSeed(random)};
                if (record.subtype != -1 && record.theme != -1) {
                    if (record.theme < 0 || record.theme >= 5)
                        throw std::runtime_error("Invalid native outdoor theme index");
                    unsigned index = 0;
                    for (const auto &sub : catalog.substitutions()) {
                        if (sub.type != record.subtype) continue;
                        if (index >= 32) throw std::runtime_error("Native room theme mask exceeds 32 bits");
                        if (room.seed.random.below(100) < sub.probability[size_t(record.theme)]) {
                            room.themeMask |= 1u << index;
                            room.dt1Mask |= sub.dt1Mask;
                        }
                        ++index;
                    }
                }
                rooms.push_back(std::move(room));
            }
        }
    return rooms;
}
} // namespace d2x
