#include "world/maze/room_graph.hpp"
#include "retail/preset_room.hpp"

namespace d2x {
std::array<int, 2> actTwoTombs(uint32_t seed) {
    Seed random(seed);
    random.next();
    int staff = 0, boss = 0;
    do { staff = random.below(7); boss = random.below(7); } while (staff == boss);
    return {66 + staff, 66 + boss};
}
bool supportsMaze(int level) {
    return (level >= 8 && level <= 12) || level == 18 || level == 19 || (level >= 21 && level <= 24) ||
           (level >= 28 && level <= 31) || (level >= 34 && level <= 36) ||
           (level >= 47 && level <= 49) || (level >= 51 && level <= 72) || level == 74 ||
           (level >= 84 && level <= 89) || level == 92 || level == 100 || level == 101 || level == 107 ||
           (level >= 113 && level <= 116) || level == 118 || level == 119 ||
           level == 122 || level == 123 || (level >= 125 && level <= 130) || level == 133 || level == 135;
}
NativeMazeLevel buildNativeMazeLevel(Archives &archives, const WorldCatalog &catalog,
    const NativeActLayout &layout, int level, uint32_t seed, int difficulty, int entranceDirection) {
    const auto &record = catalog.level(level);
    if (record.act != 0 || record.generation != GenerationKind::Maze || !supportsMaze(level))
        throw std::runtime_error("Native maze conversion currently supports Act I");
    NativeMazeLevel result;
    RetailPresetScan scanner(archives);
    std::map<std::string, MapData> patterns;
    result.recipe = maze::RoomMaze(catalog, level, seed).build(level, seed, difficulty,
        entranceDirection, &layout, [&](const MapRecipe &recipe, const MapPiece &piece, Seed &random) {
            const int x = recipe.worldX + piece.x, y = recipe.worldY + piece.y;
            uint32_t flags = 0;
            const auto found = layout.connections.find(level);
            const auto slots = found == layout.connections.end()
                ? NativeActLayout::ConnectionSlots{record.visible, record.warps} : found->second;
            for (size_t slot = 0; slot < 8; ++slot)
                if (slots.visible[slot] && slots.warps[slot] == -1) flags |= 1u << (slot + 4);
            const auto &preset = catalog.presets().at(piece.preset);
            const int width = preset.width && preset.height ? preset.width : piece.width;
            const int height = preset.width && preset.height ? preset.height : piece.height;
            auto children = allocateRetailPresetRooms(preset, piece.variant, x, y,
                width, height, flags, random,
                [&](const PresetRecord &p, int file, int px, int py, int width, int height,
                    uint32_t bits, Seed &stream) {
                    if (!p.scan && !p.pops)
                        return std::vector<uint32_t>(size_t(width / 8 + 1) * size_t(height / 8 + 1), bits);
                    const auto path = p.variants.at(size_t(file));
                    auto [entry, fresh] = patterns.try_emplace(path);
                    if (fresh) entry->second = decodeDs1(archives.read(path), path);
                    return scanner.scan(p, entry->second, record.act, width, height, bits, stream,
                        &result.presetUnits[{level, p.id, file, px, py}], px, py);
                }, piece.width <= 12 && piece.height <= 12);
            for (auto &room : children) result.rooms.push_back(std::move(room));
        });
    return result;
}
MapRecipe generateMaze(Archives &archives, const WorldCatalog &catalog, int level, uint32_t seed, int difficulty,
                       int entranceDirection) {
    if (!supportsMaze(level))
        throw std::runtime_error("This original maze family is not implemented");
    if (catalog.level(level).act == 0) {
        auto layout = placeNativeAct(catalog, 0, seed);
        if (level == 28) layout.levels.at(27) = buildRetailPresetLevel(archives, catalog, layout, 27).placement;
        return buildNativeMazeLevel(archives, catalog, layout, level, seed, difficulty, entranceDirection).recipe;
    }
    if (level == 61)
        return catalog.preset(480, catalog.level(level).levelType);
    if (level == 114 || level == 116 || level == 119) {
        Seed world(seed);
        Seed random(world.next() + uint32_t(level));
        const int preset = level == 114 ? (random.below(2) ? 1038 : 1039)
                         : level == 116 ? 1040 : 1041;
        return catalog.preset(preset, catalog.level(level).levelType,
                              random.below(catalog.presets().at(preset).files));
    }
    return maze::RoomMaze(catalog, level, seed).build(level, seed, difficulty, entranceDirection);
}
} // namespace d2x
