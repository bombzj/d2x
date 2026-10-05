#include "preset_scan.hpp"
#include "resources/data_table.hpp"
#include "world/preset_identity.hpp"
#include <algorithm>
#include <stdexcept>

// D2MOO LoadDrlgFile/AddPresetUnitToDrlgMap/BuildPresetArea.
// Numeric constants below are native unit identities, not gameplay parameters.
// MIT: docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
using Linker = std::map<std::string, std::vector<int>, std::less<>>;
int readLinker(const DataTable &table, const char *name, Linker &linker) {
    int count = 0;
    for (size_t row = 0; row < table.rows().size(); ++row) {
        const auto index = table.number(row, "hcIdx");
        if (!index) continue;
        if (*index != count || table.value(row, name).empty())
            throw std::runtime_error("Unsupported MPQ native monster linker ordering: " +
                std::string(name) + " row=" + std::to_string(row) + " hcIdx=" + std::to_string(*index) +
                " expected=" + std::to_string(count) + " identity=" + std::string(table.value(row, name)));
        // hcIdx counts records, including duplicate text identities. Do not
        // renumber later records or reject an unused duplicate in another act.
        linker[std::string(table.value(row, name))].push_back(*index);
        ++count;
    }
    return count;
}
int resolveLink(const Linker &linker, std::string_view name) {
    const auto found = linker.find(name);
    if (found == linker.end()) return -1;
    if (found->second.size() != 1)
        throw std::runtime_error("Ambiguous MPQ preset identity requires native linker verification: " + std::string(name));
    return found->second.front();
}
}
RetailPresetScan::RetailPresetScan(Archives &archives) {
    const auto read = [&](const char *name) {
        return DataTable(archives.read(std::string("data/global/excel/") + name + ".txt"));
    };
    Linker monsters, uniques, places;
    monsterCount_ = readLinker(read("monstats"), "Id", monsters);
    navi_ = resolveLink(monsters, "navi");
    uniqueCount_ = readLinker(read("superuniques"), "Superunique", uniques);
    const auto placeTable = read("monplace");
    for (size_t row = 0; row < placeTable.rows().size(); ++row) {
        const auto code = placeTable.value(row, "code");
        if (code.empty()) continue;
        const int index = int(places.size());
        if (!places.emplace(code, std::vector<int>{index}).second)
            throw std::runtime_error("Duplicate native MPQ MonPlace identity");
    }
    const auto presets = read("monpreset");
    for (size_t row = 0; row < presets.rows().size(); ++row) {
        const auto act = presets.number(row, "Act");
        if (!act) continue;
        if (*act < 1 || *act > 5) throw std::runtime_error("Invalid native MonPreset Act");
        const auto id = presets.value(row, "Place");
        int resolved = -1;
        // Original linker precedence: SuperUniques, MonStats, MonPlace.
        if (const auto index = resolveLink(uniques, id); index >= 0)
            resolved = monsterCount_ + index;
        else if (const auto index = resolveLink(monsters, id); index >= 0)
            resolved = index;
        else if (const auto index = resolveLink(places, id); index >= 0)
            resolved = monsterCount_ + uniqueCount_ + index;
        monsterPresets_[size_t(*act - 1)].push_back(resolved);
    }
    const auto objects = read("objects");
    for (size_t row = 0; row < objects.rows().size(); ++row) {
        const auto id = objects.number(row, "Id");
        if (!id) continue;
        if (!objects.has("SubClass")) throw std::runtime_error("Missing native Objects.SubClass");
        objectSubclasses_.emplace(*id, uint32_t(objects.number(row, "SubClass").value_or(0)));
    }
}
void RetailPresetScan::consumeUnitRandom(const MapData &data, int act, Seed &random) const {
    (void)buildUnits(data, act, 0, 0, random);
}
std::vector<RetailPresetUnit> RetailPresetScan::buildClientUnits(const MapData &data,
    int level, int preset, int file, int mapX, int mapY, int width, int height) const {
    // D2MOO SpawnHardcodedPresetUnits / SpawnRiver / AddPresetRiverObjects.
    // Engine preset/object identities are fixed; Navi resolves through MonStats.
    std::vector<RetailPresetUnit> result;
    if (!width) width = data.width - 1;
    if (!height) height = data.height - 1;
    const auto add = [&](int type, int id, int x, int y, bool clientOnly) {
        RetailPresetUnit unit;
        unit.unit.type = type; unit.unit.id = id; unit.unit.x = x; unit.unit.y = y;
        unit.unit.nativeIdentity = true;
        unit.mode = type == 1 ? 1 : 0; unit.clientOnly = clientOnly;
        result.insert(result.begin(), std::move(unit));
    };
    if (level == 2 && preset >= 4 && preset <= 7 && file == 3) {
        if (navi_ < 0) throw std::runtime_error("MPQ MonStats lacks native Navi identity");
        add(1, navi_, (mapX + width / 2) * 5, (mapY + height / 2) * 5, false);
    } else if (preset == 1 || preset == 3 || (preset >= 26 && preset <= 28) || preset == 300) {
        if (data.floors.empty()) throw std::runtime_error("Native river preset has no floor grid");
        const auto cell = [&](int x, int y) -> const MapCell & {
            return data.floors.front().at(size_t(y) * size_t(data.width) + size_t(x));
        };
        const auto river = [&](int offset) {
            // River sound follows the native eight-tile room interval (40
            // subtiles), not the five subtiles within one tile.
            for (int y = 0; y < height * 5; y += 40)
                add(2, 65, (mapX + offset + 1) * 5, mapY * 5 + y, true);
            for (int y = 0; y < height; ++y) {
                const int x = (mapX + offset) * 5 - 5, sy = (mapY + y) * 5;
                add(2, 40, x, sy, true);
                for (int dx : {5, 10, 15}) add(2, 41, x + dx, sy, true);
                add(2, 42, x + 20, sy, true);
                const auto packed = cell(std::max(offset, 0), y).value;
                if (((packed >> 20) & 63) == 4) {
                    const auto sequence = (packed >> 8) & 255;
                    if (sequence == 0 || sequence == 4 || sequence == 8 || sequence == 16 ||
                        sequence == 29 || sequence == 39) y += 3;
                }
            }
        };
        if (preset == 27) river(-1);
        else for (int x = 0; x < width; ++x) {
            const auto packed = cell(x, 0).value;
            if (((packed >> 20) & 63) == 2 && ((packed >> 8) & 255) == 24) {
                river(x); x += 4;
            }
        }
    }
    return result;
}
std::optional<RetailPresetUnit> RetailPresetScan::resolveUnit(const MapObject &source,
    int version, int act, int mapX, int mapY) const {
    if (act < 0 || act >= int(monsterPresets_.size())) throw std::invalid_argument("Invalid native preset act");
    RetailPresetUnit result{source};
    auto &unit = result.unit;
    if (unit.type == 1) {
        if (version <= 4) return {}; // The native file loader discards these.
        const auto &ids = monsterPresets_[size_t(act)];
        if (unit.id < 0 || size_t(unit.id) >= ids.size())
            throw std::runtime_error("Native DS1 monster has no MPQ MonPreset entry");
        unit.id = ids[size_t(unit.id)];
        result.mode = 1;
        // LoadDrlgFile converts these MonPreset markers to object identities.
        if (act == 2 && (unit.id == 297 || unit.id == 366)) {
            unit.id = unit.id == 297 ? 382 : 404;
            unit.type = 2;
            result.mode = 0;
        }
        if (act == 4) {
            const int marker = unit.id;
            if (marker == 514 || (marker >= 537 && marker <= 539)) {
                unit.id = marker == 514 ? 461 : marker == 537 ? 476 : marker == 538 ? 475 : 474;
                unit.type = 2;
                result.mode = 0;
            }
        }
    } else if (unit.type == 2) {
        if (version <= 5 && unit.id == 573) return {};
        unit.id = originalObjectClass(source, version, act);
    } else if (unit.type == 4) {
        result.mode = 3;
        result.identityResolved = version <= 4;
    }
    if (unit.id < 0) return {};
    unit.nativeIdentity = result.identityResolved;
    unit.x += mapX * 5; unit.y += mapY * 5;
    for (auto &node : unit.path) { node.x += mapX * 5; node.y += mapY * 5; }
    return result;
}
std::vector<RetailPresetUnit> RetailPresetScan::buildUnits(const MapData &data, int act,
    int mapX, int mapY, Seed &random) const {
    act = data.act; // File-authored act selects MonPreset and object ordinal tables.
    std::vector<RetailPresetUnit> result;
    // The original DS1 loader prepends units: consume in reverse file order.
    for (auto unit = data.objects.rbegin(); unit != data.objects.rend(); ++unit) {
        auto resolved = resolveUnit(*unit, data.version, act, mapX, mapY);
        if (!resolved) continue;
        const int id = resolved->unit.id;
        if (resolved->unit.type == 1) {
            if (id < monsterCount_) {
                if ((id == 204 || id == 205 || id == 371 || id == 372) && random.below(3)) continue;
            } else if (id >= monsterCount_ + uniqueCount_) {
                const int marker = id - monsterCount_ - uniqueCount_;
                if (marker == 33 && !(random.next() & 3)) continue;
                if (marker == 34 && !(random.next() & 1)) continue;
                if (marker == 35 && (random.next() & 3)) continue;
            }
        } else if (resolved->unit.type == 2) {
            if ((id == 196 || id == 261) && (random.next() & 1)) continue;
            if (id == 581 && !(random.next() & 3)) continue;
        }
        // AddPresetUnitToDrlgMap prepends each accepted file-list entry.
        result.insert(result.begin(), std::move(*resolved));
    }
    return result;
}
std::vector<uint32_t> RetailPresetScan::scan(const PresetRecord &preset, const MapData &data,
    int act, int width, int height, uint32_t flags, Seed &random,
    std::vector<RetailPresetUnit> *units, int mapX, int mapY) const {
    const int gw = width / 8 + 1, gh = height / 8 + 1;
    std::vector<uint32_t> result(size_t(gw) * size_t(gh), flags);
    if (!preset.scan && !preset.pops) return result;
    if (data.width != width + 1 || data.height != height + 1)
        throw std::runtime_error("Native preset DS1 extent disagrees with LvlPrest");
    auto resolved = buildUnits(data, act, mapX, mapY, random);
    if (units) *units = std::move(resolved);
    auto mark = [&](int x, int y, uint32_t bits) {
        if (x < 0 || y < 0 || x / 8 >= gw || y / 8 >= gh)
            throw std::runtime_error("Native preset scan position exceeds extent");
        result[size_t(y / 8) * size_t(gw) + size_t(x / 8)] |= bits;
    };
    if (preset.scan) {
        for (const auto &layer : data.walls)
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    const auto &cell = layer[size_t(y) * size_t(data.width) + size_t(x)];
                    const auto style = (cell.value >> 20) & 63;
                    const auto sequence = (cell.value >> 8) & 255;
                    if ((cell.orientation == 10 || cell.orientation == 11) && style <= 7 &&
                        (sequence == 0 || sequence == 4 || (cell.value & 0x80000000u)))
                        mark(x, y, 1u << (style + 4));
                }
        for (const auto &unit : data.objects) {
            if (unit.type != 2) continue;
            const int id = originalObjectClass(unit, data.version, data.act);
            if (id < 0 || id >= 573) continue;
            const auto found = objectSubclasses_.find(id);
            if (found == objectSubclasses_.end())
                throw std::runtime_error("Native preset object has no MPQ subclass");
            if (found->second & 0x40) mark(unit.x / 5, unit.y / 5, 0x30000);
        }
    }
    return result;
}
} // namespace d2x
