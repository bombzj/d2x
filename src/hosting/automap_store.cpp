#include "automap_store.hpp"
#include "resources/atomic_file.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace d2x::hosting {
namespace {
constexpr size_t maxBytes = 16 * 1024 * 1024;
using Bytes = std::vector<uint8_t>;
using Index = std::array<uint32_t, 6>;
using Json=nlohmann::json;
void require(bool valid) {
    if (!valid) throw std::runtime_error("Invalid native automap cache; file preserved");
}
Bytes read(const std::filesystem::path &path) {
    const auto status = std::filesystem::symlink_status(path);
    if (!std::filesystem::exists(status)) return {};
    require(std::filesystem::is_regular_file(status));
    const auto size = std::filesystem::file_size(path);
    require(size <= maxBytes);
    Bytes bytes(size);
    std::ifstream file(path, std::ios::binary);
    require(bool(file) && bool(file.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(size))));
    return bytes;
}
uint32_t u32(const Bytes &b, size_t p) {
    require(p <= b.size() && b.size() - p >= 4);
    return uint32_t(b[p]) | uint32_t(b[p+1]) << 8 | uint32_t(b[p+2]) << 16 | uint32_t(b[p+3]) << 24;
}
void put32(Bytes &b, size_t p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b.at(p+i) = uint8_t(v >> (i*8));
}
Index index(const Bytes &bytes) {
    if (bytes.empty()) return {12, 0, 0, 0, 0, 0};
    require(bytes.size() == 24);
    Index result{};
    for (size_t i = 0; i < result.size(); ++i) result[i] = u32(bytes, i*4);
    require(result[0] == 12 && result[1] < 4);
    return result;
}
std::filesystem::path slotPath(std::filesystem::path path, size_t slot) {
    path.replace_extension(".ma" + std::to_string(slot));
    return path;
}
size_t selectSlot(const std::filesystem::path &path, uint32_t seed) {
    const auto original = read(path);
    require(!original.empty() || !std::filesystem::exists(path));
    auto header = index(original);
    if (!original.empty())
        for (size_t slot=0; slot<4; ++slot) if (header[slot+2]==seed) return slot;
    const size_t slot = header[1];
    // Native selects/rotates on layer load, even before the player explores.
    // Clear the retired slot before publishing its new seed; interruption can
    // lose cache availability but cannot expose a different world's terrain.
    writeFileAtomically(slotPath(path,slot), Bytes(400), true);
    header[slot+2]=seed; header[1]=uint32_t((slot+1)%4);
    Bytes updated(24);
    for (size_t i=0; i<6; ++i) put32(updated,i*4,header[i]);
    writeFileAtomically(path,updated,true);
    return slot;
}
std::vector<NativeAutomapLayer> decode(const Bytes &b, uint32_t seed) {
    std::vector<NativeAutomapLayer> result;
    if (b.empty()) return result;
    require(b.size() >= 400);
    std::set<size_t> visited;
    std::map<size_t, size_t> intervals;
    for (uint32_t layer = 0; layer < 100; ++layer) {
        size_t p = u32(b, layer*4);
        while (p) {
            require(p >= 400 && p <= b.size() && b.size()-p >= 32 && visited.insert(p).second);
            const size_t start = p;
            const auto next = u32(b,p);
            NativeAutomapLayer entry{seed, u32(b,p+4), u32(b,p+8), u32(b,p+12), {}};
            require(entry.layer == layer);
            std::array<uint32_t,4> sizes{};
            for (size_t group=0; group<4; ++group) sizes[group]=u32(b,p+16+group*4);
            p += 32;
            for (size_t group=0; group<4; ++group) {
                const auto size = sizes[group];
                require(size % 6 == 0 && size <= b.size()-p);
                const size_t end = p+size;
                for (; p<end; p+=6) {
                    const auto word = [&](size_t offset) { return uint16_t(b[offset] | uint16_t(b[offset+1]) << 8); };
                    require(word(p) <= INT16_MAX);
                    entry.cells[group].push_back({word(p), int16_t(word(p+2)), int16_t(word(p+4))});
                }
            }
            intervals.emplace(start,p);
            result.push_back(std::move(entry));
            p = next;
        }
    }
    size_t end = 400;
    for (const auto &[start, stop] : intervals) { require(start >= end); end = stop; }
    return result;
}
Bytes encode(const std::vector<NativeAutomapLayer> &layers) {
    Bytes bytes(400);
    std::array<size_t,100> last{};
    for (const auto &layer : layers) {
        require(layer.layer < 100);
        size_t length=32;
        for (const auto &group:layer.cells) {
            require(group.size() <= maxBytes/6);
            length += group.size()*6;
        }
        require(length <= maxBytes-bytes.size());
        const auto offset=bytes.size();
        if (last[layer.layer]) put32(bytes,last[layer.layer],uint32_t(offset));
        else put32(bytes,layer.layer*4,uint32_t(offset));
        last[layer.layer]=offset;
        bytes.resize(offset+length);
        put32(bytes,offset+4,layer.layer); put32(bytes,offset+8,layer.flags); put32(bytes,offset+12,layer.objectSeed);
        size_t p=offset+32;
        for (size_t group=0;group<4;++group) {
            put32(bytes,offset+16+group*4,uint32_t(layer.cells[group].size()*6));
            for (const auto &cell:layer.cells[group]) {
                require(cell.cel <= INT16_MAX);
                for (uint16_t word : {cell.cel,uint16_t(cell.x),uint16_t(cell.y)}) {
                    bytes[p++]=uint8_t(word); bytes[p++]=uint8_t(word>>8);
                }
            }
        }
    }
    return bytes;
}
std::filesystem::path navigationPath(std::filesystem::path path,size_t slot) {
    path.replace_extension(".d2xnav"+std::to_string(slot));return path;
}
template<size_t N> std::vector<std::array<uint16_t,N>> hints(const Json &rows) {
    require(rows.is_array() && rows.size()<=65536);
    std::vector<std::array<uint16_t,N>> result;
    for(const auto &row:rows) {
        require(row.is_array() && row.size()==N);std::array<uint16_t,N> value{};
        for(size_t i=0;i<N;++i) {
            require(row[i].is_number_integer() && row[i].get<int64_t>()>=0 && row[i].get<uint64_t>()<=UINT16_MAX);
            value[i]=row[i].get<uint16_t>();
        }
        result.push_back(value);
    }
    return result;
}
std::optional<NavigationDiscovery> decodeNavigation(const Bytes &bytes,uint32_t seed,uint32_t objectSeed) {
    if(bytes.empty()) return {};
    const auto json=Json::parse(bytes);require(json.is_object() && json.at("version")==1);
    if(json.at("seed")!=seed || json.at("objectSeed")!=objectSeed) return {};
    NavigationDiscovery nav;nav.seed=seed;nav.objectSeed=objectSeed;nav.profile=json.at("profile").get<std::string>();
    require(!nav.profile.empty() && nav.profile.size()<=32);
    const auto &acts=json.at("actSeeds");require(acts.is_array() && acts.size()<=5);
    for(const auto &entry:acts) {
        require(entry.is_array() && entry.size()==2 && entry[0].is_number_integer() && entry[1].is_number_integer());
        const auto act=entry[0].get<int64_t>(),value=entry[1].get<int64_t>();
        require(act>=0 && act<5 && value>=0 && uint64_t(value)<=UINT32_MAX && !nav.actSeeds.contains(int(act)));
        nav.actSeeds.emplace(int(act),uint32_t(value));
    }
    const auto &layers=json.at("observed");require(layers.is_array() && layers.size()<=1024);
    size_t cells=0;
    for(const auto &entry:layers) {
        const auto area=entry.at("area").get<int64_t>(),width=entry.at("width").get<int64_t>(),height=entry.at("height").get<int64_t>();
        require(area>0 && area<=UINT16_MAX && width>0 && width<=8192 && height>0 && height<=8192);
        const auto count=uint64_t(width)*uint64_t(height);require(count<=maxBytes-cells);cells+=size_t(count);
        AutomapLayer layer{int(width),int(height),entry.at("fingerprint").get<uint64_t>(),std::vector<uint8_t>(size_t(count))};
        const auto &runs=entry.at("runs");require(runs.is_array() && runs.size()<=count);size_t end=0;
        for(const auto &run:runs) {
            require(run.is_array() && run.size()==2 && run[0].is_number_integer() && run[1].is_number_integer());
            const auto start=run[0].get<int64_t>(),length=run[1].get<int64_t>();
            require(start>=0 && uint64_t(start)>=end && uint64_t(start)<=count && length>0 && uint64_t(length)<=count-uint64_t(start));
            end=size_t(start+length);std::fill(layer.seen.begin()+start,layer.seen.begin()+std::ptrdiff_t(end),uint8_t{1});
        }
        require(nav.observed.emplace(RegionId(area),std::move(layer)).second);
    }
    nav.targets=hints<5>(json.at("targets"));nav.connections=hints<4>(json.at("connections"));nav.waypoints=hints<3>(json.at("waypoints"));
    return nav;
}
Bytes encodeNavigation(const NavigationDiscovery &nav) {
    Json json{{"version",1},{"profile",nav.profile},{"seed",nav.seed},{"objectSeed",nav.objectSeed},
        {"actSeeds",Json::array()},{"observed",Json::array()},{"targets",nav.targets},{"connections",nav.connections},{"waypoints",nav.waypoints}};
    for(const auto &[act,seed]:nav.actSeeds) json["actSeeds"].push_back({act,seed});
    for(const auto &[area,layer]:nav.observed) {
        require(layer.width>0 && layer.height>0 && uint64_t(layer.width)*layer.height==layer.seen.size());
        Json runs=Json::array();
        for(size_t i=0;i<layer.seen.size();) {
            require(layer.seen[i]<=1);if(!layer.seen[i]) {++i;continue;}
            const size_t begin=i;while(i<layer.seen.size() && layer.seen[i]==1) ++i;runs.push_back({begin,i-begin});
        }
        json["observed"].push_back({{"area",uint16_t(area)},{"width",layer.width},{"height",layer.height},
            {"fingerprint",layer.layoutFingerprint},{"runs",std::move(runs)}});
    }
    const auto text=json.dump();require(text.size()<=maxBytes);Bytes bytes(text.begin(),text.end());
    (void)decodeNavigation(bytes,nav.seed,nav.objectSeed);return bytes;
}
void component(std::string_view name) {
    require(!name.empty() && name != "." && name != ".." && name.back() != '.' && name.back() != ' ');
    require(name.find_first_of("/\\:<>\"|?*") == std::string_view::npos);
    for (unsigned char c:name) require(c >= 32);
}
} // namespace
AutomapStore::AutomapStore(std::filesystem::path character) : path_(std::move(character)) {
    path_.replace_extension(".map");
}
AutomapStore AutomapStore::realm(const std::filesystem::path &root, std::string_view realm,
                                std::string_view character) {
    component(realm); component(character);
    return AutomapStore(root / std::string(realm) / (std::string(character)+".d2s"));
}
std::optional<AutomapDiscovery> AutomapStore::load(uint32_t seed, uint32_t objectSeed) const {
    const auto slot=selectSlot(path_,seed);
    AutomapDiscovery result;
    result.nativeLayers=decode(read(slotPath(path_,slot)),seed);
    // Native 0x61E70 restores static terrain across games but object cells
    // only when S2C 03's secondary seed agrees with the block header.
    for (auto &layer:result.nativeLayers) if (layer.objectSeed!=objectSeed) {
        layer.cells[2].clear(); layer.objectSeed=objectSeed;
    }
    result.navigation=decodeNavigation(read(navigationPath(path_,slot)),seed,objectSeed);
    return result;
}
void AutomapStore::save(const AutomapDiscovery &discovery) const {
    std::map<uint32_t,std::vector<NativeAutomapLayer>> worlds;
    for (const auto &layer:discovery.nativeLayers) worlds[layer.seed].push_back(layer);
    for (const auto &[seed,layers]:worlds) {
        const size_t slot=selectSlot(path_,seed);
        auto target=slotPath(path_,slot);
        auto saved=decode(read(target),seed);
        const bool hadCells = !saved.empty();
        for (const auto &layer:layers) {
            auto found=std::find_if(saved.begin(),saved.end(),[&](const auto &old) {
                return old.layer==layer.layer && old.objectSeed==layer.objectSeed;
            });
            if (found==saved.end()) saved.push_back(layer);
            else for (size_t group=0;group<4;++group)
                found->cells[group].insert(found->cells[group].end(),layer.cells[group].begin(),layer.cells[group].end());
        }
        // Compact native chains without dropping layers we did not visit.
        for (auto &layer:saved) for (auto &group:layer.cells) {
            std::sort(group.begin(),group.end()); group.erase(std::unique(group.begin(),group.end()),group.end());
        }
        const auto data=encode(saved);
        writeFileAtomically(target,data,hadCells);
    }
    if(discovery.navigation) {
        const auto &nav=*discovery.navigation;const auto slot=selectSlot(path_,nav.seed);
        const auto target=navigationPath(path_,slot);const auto original=read(target);
        // Reject unknown versions instead of silently migrating an old cache.
        (void)decodeNavigation(original,nav.seed,nav.objectSeed);
        const auto bytes=encodeNavigation(nav);writeFileAtomically(target,bytes,!original.empty());
    }
}
} // namespace d2x::hosting
