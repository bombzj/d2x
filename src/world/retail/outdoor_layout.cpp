#include "outdoor_layout.hpp"
#include "wilderness.hpp"
#include "preset_scan.hpp"
#include <map>
#include <stdexcept>

namespace d2x {
RetailOutdoorLayout buildRetailOutdoorLayout(Archives &archives, const WorldCatalog &catalog,
                                            const NativeActLayout &act, int level) {
    const auto &record = catalog.level(level);
    if (record.generation != GenerationKind::Outdoor || record.act != 0 || record.levelType != 2)
        throw std::runtime_error("Retail outdoor macro generator currently supports Act I wilderness");
    const auto &placement = act.levels.at(level);
    RetailOutdoorLayout result{placement,
        RetailOutdoorGrid(placement.width, placement.height, act.startSeed, level), {}, 0};
    std::map<std::string, MapData> patterns;
    auto reader = [&](const std::string &path) -> const MapData & {
        const auto key = normalize(path);
        auto [entry, fresh] = patterns.try_emplace(key);
        if (fresh) entry->second = decodeDs1(archives.read(key), key);
        return entry->second;
    };
    result.flags = initializeRetailWilderness(catalog, act, level, result.grid, reader);
    if (level >= 2 && level <= 7)
        result.paths = buildRetailDirtPaths(act, level, result.flags, result.grid);
    finishRetailWildernessPresets(catalog, act, level, result.grid);
    // Connector overrides happen after the initial placement. Validate the
    // final selected path, including File4/File5 outside the random Files limit.
    for (int y = 0; y < result.grid.height(); ++y)
        for (int x = 0; x < result.grid.width(); ++x) {
            const auto &cell = result.grid.cell(x, y);
            if (!cell.preset) continue;
            const auto &preset = catalog.presets().at(cell.preset);
            const auto file = size_t((cell.flags >> 16) & 15);
            if (file >= preset.variants.size() || preset.variants[file].empty() ||
                !archives.contains(preset.variants[file]))
                throw std::runtime_error("Native selected preset file is unavailable: " + std::to_string(cell.preset));
        }
    RetailPresetScan scanner(archives);
    result.rooms = allocateRetailOutdoorRooms(catalog, act, level, result.grid,
        [&](const PresetRecord &preset, int file, int x, int y, int width, int height, uint32_t flags, Seed &seed) {
            // Non-scanned presets never open their DS1 during native allocation.
            if (!preset.scan && !preset.pops)
                return std::vector<uint32_t>(size_t(width / 8 + 1) * size_t(height / 8 + 1), flags);
            return scanner.scan(preset, reader(preset.variants.at(size_t(file))),
                record.act, width, height, flags, seed,
                &result.presetUnits[{level, preset.id, file, x, y}], x, y);
        });
    return result;
}
} // namespace d2x
