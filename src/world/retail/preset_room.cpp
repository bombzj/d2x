#include "preset_room.hpp"
#include "preset_scan.hpp"
#include <stdexcept>

// D2MOO InitPresetRoomGrids and AddPresetRoomMapTiles.
// MIT: docs/licenses/D2MOO.txt.
namespace d2x {
RetailPresetLevel buildRetailPresetLevel(Archives &archives, const WorldCatalog &catalog,
    const NativeActLayout &act, int level) {
    const auto &record = catalog.level(level);
    if (record.generation != GenerationKind::Preset)
        throw std::runtime_error("Native preset generation requires a preset level");
    const PresetRecord *preset = nullptr;
    for (const auto &[id, candidate] : catalog.presets()) {
        if (candidate.level != level) continue;
        if (preset) throw std::runtime_error("Multiple MPQ presets claim the same native level");
        preset = &candidate;
    }
    if (!preset) throw std::runtime_error("Native preset level has no MPQ LvlPrest record");
    // InitLevelData chooses the direction during allocation. InitLevel later
    // restores the level seed before AllocDrlgMap / BuildArea, so that draw
    // must not advance the generation stream.
    Seed directionRandom(act.startSeed + uint32_t(level));
    int direction = preset->files ? directionRandom.below(preset->files) : -1;
    const auto &placement = act.levels.at(level);
    if (placement.presetVariant) direction = *placement.presetVariant;
    Seed random(act.startSeed + uint32_t(level));
    const int selected = random.below(preset->files);
    const int file = direction < 0 ? selected : direction;
    if (file < 0 || size_t(file) >= preset->variants.size() || preset->variants[size_t(file)].empty())
        throw std::runtime_error("Native selected preset level file is absent from MPQ");
    RetailPresetLevel result{placement, preset->id, file, {}};
    if (preset->width && preset->height) {
        result.placement.width = preset->width;
        result.placement.height = preset->height;
    }
    const auto found = act.connections.find(level);
    const auto slots = found == act.connections.end()
        ? NativeActLayout::ConnectionSlots{record.visible, record.warps} : found->second;
    uint32_t flags = 0;
    for (size_t slot = 0; slot < 8; ++slot)
        if (slots.visible[slot] && slots.warps[slot] == -1) flags |= 1u << (slot + 4);
    RetailPresetScan scanner(archives);
    result.rooms = allocateRetailPresetRooms(*preset, file, placement.x, placement.y,
        result.placement.width, result.placement.height, flags, random,
        [&](const PresetRecord &p, int chosen, int x, int y, int width, int height, uint32_t bits, Seed &seed) {
            if (!p.scan && !p.pops)
                return std::vector<uint32_t>(size_t(width / 8 + 1) * size_t(height / 8 + 1), bits);
            const auto path = p.variants.at(size_t(chosen));
            const auto source = decodeDs1(archives.read(path), path);
            return scanner.scan(p, source, record.act, width, height, bits, seed, &result.units, x, y);
        });
    return result;
}
RetailPresetRoomData buildRetailPresetRoomData(const PresetRecord &preset, const RetailRoom &room,
                                             const MapData &source) {
    if (room.preset != preset.id || room.width <= 0 || room.height <= 0)
        throw std::runtime_error("Native preset room identity or extent is invalid");
    const int ox = room.x - room.mapX, oy = room.y - room.mapY;
    if (ox < 0 || oy < 0 || ox + room.width >= source.width || oy + room.height >= source.height)
        throw std::runtime_error("Native preset room exceeds its DS1");
    RetailPresetRoomData result{room, {},
        preset.killEdge && ox + room.width == source.width - 1,
        preset.killEdge && oy + room.height == source.height - 1, preset.fillBlanks,
        preset.scan || preset.pops != 0};
    result.grids.width = room.width + 1;
    result.grids.height = room.height + 1;
    result.grids.version = source.version;
    result.grids.act = source.act;
    auto crop = [&](const std::vector<MapCell> &layer, uint32_t layerBits, uint32_t edgeBits) {
        std::vector<MapCell> output(size_t(result.grids.width) * size_t(result.grids.height));
        for (int y = 0; y < result.grids.height; ++y)
            for (int x = 0; x < result.grids.width; ++x) {
                auto &cell = output[size_t(y) * size_t(result.grids.width) + size_t(x)];
                cell = layer.at(size_t(oy + y) * size_t(source.width) + size_t(ox + x));
                cell.value |= layerBits;
                if (!x || !y || x == room.width || y == room.height) cell.value |= edgeBits;
            }
        return output;
    };
    for (size_t i = 0; i < source.floors.size(); ++i)
        result.grids.floors.push_back(crop(source.floors[i], uint32_t(i) << 18, 132));
    for (size_t i = 0; i < source.walls.size(); ++i) {
        // Only the first wall layer receives the native 132 edge mask.
        auto layer = crop(source.walls[i], uint32_t(i) << 18, i ? 0u : 132u);
        result.grids.walls.push_back(std::move(layer));
    }
    if (!source.shadows.empty()) result.grids.shadows = crop(source.shadows, 0, 132);
    if (preset.pops) {
        if (source.roofPopups.size() > size_t(preset.pops))
            throw std::runtime_error("Native popup count exceeds MPQ LvlPrest.Pops");
        for (auto popup : source.roofPopups) {
            popup.x += room.mapX; popup.y += room.mapY;
            popup.pad = preset.popPad;
            popup.parentX = room.mapX; popup.parentY = room.mapY;
            result.roofPopups.push_back(popup);
        }
    }
    return result;
}
} // namespace d2x
