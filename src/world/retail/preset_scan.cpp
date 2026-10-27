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
int readLinker(const DataTable &table, const char *name, Linker &linker, bool indexedByRow) {
    int count = 0;
    for (size_t row = 0; row < table.rows().size(); ++row) {
        const auto index = table.number(row, "hcIdx");
        if (!index) continue;
        if ((!indexedByRow && *index != count) || table.value(row, name).empty())
            throw std::runtime_error("Unsupported MPQ native monster linker ordering: " +
                std::string(name) + " row=" + std::to_string(row) + " hcIdx=" + std::to_string(*index) +
                " expected=" + std::to_string(count) + " identity=" + std::string(table.value(row, name)));
        // MonStats' Id linker uses record order; hcIdx is not a compiled field.
        // CN monstats.bin row 1198 has ID 1198 despite TXT hcIdx 1199.
        // Preserve duplicate names so resolveLink still selects their first entry.
        linker[std::string(table.value(row, name))].push_back(indexedByRow ? count : *index);
        ++count;
    }
    return count;
}
int resolveLink(const Linker &linker, std::string_view name) {
    const auto found = linker.find(name);
    if (found == linker.end()) return -1;
    // FOG's name linker reuses the first entry. Current monstats.bin retains
    // 617 as the name-link ID in both cr_lancer8 rows (617 and 723).
    return found->second.front();
}
}
RetailPresetScan::RetailPresetScan(Archives &archives):pd2_(archives.pd2Presets) {
    const auto read = [&](const char *name) {
        return DataTable(archives.read(std::string("data/global/excel/") + name + ".txt"));
    };
    Linker monsters, uniques, places;
    monsterCount_ = readLinker(read("monstats"), "Id", monsters, true);
    navi_ = resolveLink(monsters, "navi");
    uniqueCount_ = readLinker(read("superuniques"), "Superunique", uniques, false);
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
    if(pd2_) {
        // Current MPQ MonPreset BIN contains the actual marker classes. Its
        // TXT still names the retail hydras at these positions (738..740,
        // 786 in BIN), so recompiling the name links changes authored maps.
        const auto compiled=archives.read("data/global/excel/monpreset.bin");
        if(compiled.size()<4) throw std::runtime_error("Truncated native MonPreset BIN");
        const uint32_t count=uint32_t(compiled[0]) | uint32_t(compiled[1])<<8 |
            uint32_t(compiled[2])<<16 | uint32_t(compiled[3])<<24;
        if(count!=(compiled.size()-4)/4 || (compiled.size()-4)%4)
            throw std::runtime_error("Invalid native MonPreset BIN size");
        for(auto &section:monsterPresets_) section.clear();
        int previousAct=1;
        for(size_t row=0;row<count;++row) {
            const auto offset=4+row*4;
            const int act=compiled[offset],type=compiled[offset+1];
            const int place=int(compiled[offset+2]) | int(compiled[offset+3])<<8;
            if(act<previousAct || act>5 || type>2)
                throw std::runtime_error("Invalid native MonPreset BIN record");
            previousAct=act;
            const int identity=place+(type==0?monsterCount_+uniqueCount_:type==2?monsterCount_:0);
            monsterPresets_[size_t(act-1)].push_back(identity);
        }
    }
    const auto objects = read("objects");
    for (size_t row = 0; row < objects.rows().size(); ++row) {
        const auto id = objects.number(row, "Id");
        if (!id) continue;
        if (!objects.has("SubClass")) throw std::runtime_error("Missing native Objects.SubClass");
        objectSubclasses_.emplace(*id, uint32_t(objects.number(row, "SubClass").value_or(0)));
    }
}
bool RetailPresetScan::waypointObject(int id) const {
    const auto found=objectSubclasses_.find(id);
    return found!=objectSubclasses_.end() && (found->second&64)!=0;
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
        if(unit.id<0) {
            // LoadDrlgFile checks only index<count. Signed negative ordinals
            // address the preceding act in the same compiled table. Bound
            // that linear access to the actual table instead of using UB.
            int64_t ordinal=unit.id;int section=act;
            while(ordinal<0 && section>0) ordinal+=int64_t(monsterPresets_[size_t(--section)].size());
            if(ordinal<0 || size_t(ordinal)>=monsterPresets_[size_t(section)].size()) return {};
            unit.id=monsterPresets_[size_t(section)][size_t(ordinal)];
        }
        // LoadDrlgFile only translates IDs inside the act's MonPreset
        // section. Its release branch retains an out-of-range ID as the
        // native class; PD2 map files contain these direct class markers.
        else if (size_t(unit.id) < ids.size()) unit.id = ids[size_t(unit.id)];
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
        unit.id = pd2_?pd2ObjectClass(source,version,act):originalObjectClass(source,version,act);
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
    std::vector<RetailPresetUnit> *units, int mapX, int mapY,std::vector<NativeTileInfo> *tileInfo) const {
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
                    if(tileInfo && (cell.orientation==10 || cell.orientation==11) && style>=30 && style<=33) {
                        const int index=style==30?int(sequence):style==31?int(sequence)+5:style==32?10:11;
                        tileInfo->push_back({index,mapX+x,mapY+y});
                    }
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
