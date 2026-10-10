#include "automap_store.hpp"
#include "core/fingerprint.hpp"
#include "resources/atomic_file.hpp"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <tuple>

namespace d2x::hosting {
namespace {
using Json = nlohmann::json;
constexpr size_t maxBytes = 16 * 1024 * 1024;
constexpr std::string_view rules = "native-visible-terrain-v1";
uint64_t checksum(const Json &value) {
    Fingerprint hash;
    hash.add(value.dump());
    return hash.value();
}
Json encode(const AutomapDiscovery &d, bool tcpIp) {
    Json j{{"tcpIp", tcpIp},        {"difficulty", d.difficulty}, {"content", d.contentFingerprint},
           {"acts", Json::array()}, {"layers", Json::array()},    {"cells", Json::array()},
           {"towns", Json::array()}};
    for (const auto &[act, seed] : d.actSeeds)
        j["acts"].push_back({act, seed});
    for (const auto &[id, layer] : d.layers) {
        static constexpr char hex[] = "0123456789abcdef";
        std::string mask((layer.seen.size() + 3) / 4, '0');
        for (size_t i = 0; i < mask.size(); ++i) {
            unsigned bits = 0;
            for (size_t b = 0; b < 4 && i * 4 + b < layer.seen.size(); ++b)
                bits |= unsigned(layer.seen[i * 4 + b] != 0) << b;
            mask[i] = hex[bits];
        }
        j["layers"].push_back({int(id), layer.width, layer.height, layer.layoutFingerprint, std::move(mask)});
    }
    for (const auto &c : d.cells)
        j["cells"].push_back({c.seed, c.act, c.region, c.x, c.y, c.cels});
    for (const auto &t : d.towns)
        j["towns"].push_back({t.seed, t.act, t.region, t.variant, t.x, t.y});
    return j;
}
AutomapDiscovery decode(const Json &j) {
    AutomapDiscovery d;
    d.difficulty = j.at("difficulty").get<int>();
    d.contentFingerprint = j.at("content").get<uint64_t>();
    if (d.difficulty < 0 || d.difficulty > 2 || !j.at("tcpIp").is_boolean())
        throw std::runtime_error("Invalid automap world");
    for (const auto &a : j.at("acts")) {
        const int act = a.at(0).get<int>();
        const auto seed = a.at(1).get<uint32_t>();
        if (a.size() != 2 || act < 0 || act > 4 || !d.actSeeds.emplace(act, seed).second)
            throw std::runtime_error("Invalid automap act");
    }
    if (d.actSeeds.empty() || d.actSeeds.size() > 5 || j.at("layers").size() > 256 ||
        j.at("cells").size() > 1000000 || j.at("towns").size() > 5)
        throw std::runtime_error("Automap limits exceeded");
    uint64_t total = 0;
    for (const auto &l : j.at("layers")) {
        const int id = l.at(0).get<int>(), w = l.at(1).get<int>(), h = l.at(2).get<int>();
        const auto hash = l.at(3).get<uint64_t>();
        const auto mask = l.at(4).get<std::string>();
        const auto count = w > 0 && h > 0 ? uint64_t(w) * h : 0;
        total += count;
        if (l.size() != 5 || id <= 0 || id > 65535 || !count || count > 32000000 || total > 32000000 ||
            mask.size() != (count + 3) / 4)
            throw std::runtime_error("Invalid automap layer");
        AutomapLayer layer{w, h, hash, std::vector<uint8_t>(size_t(count))};
        for (size_t i = 0; i < mask.size(); ++i) {
            const auto c = mask[i];
            const int bits = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (bits < 0 || (i == mask.size() - 1 && count % 4 && unsigned(bits) >> (count % 4)))
                throw std::runtime_error("Invalid automap mask");
            for (size_t b = 0; b < 4 && i * 4 + b < count; ++b)
                layer.seen[i * 4 + b] = uint8_t((bits >> b) & 1);
        }
        if (!d.layers.emplace(RegionId(id), std::move(layer)).second)
            throw std::runtime_error("Duplicate automap layer");
    }
    const auto valid = [&](uint32_t seed, int act, int region, int x, int y) {
        return d.actSeeds.contains(act) && d.actSeeds.at(act) == seed && region > 0 && region <= 65535 &&
               x >= 0 && x <= 65535 && y >= 0 && y <= 65535;
    };
    std::set<std::tuple<int, int, int, int>> cells;
    for (const auto &c : j.at("cells")) {
        AutomapRememberedCell cell{c.at(0).get<uint32_t>(), c.at(1).get<int>(),
                                   c.at(2).get<int>(),      c.at(3).get<int>(),
                                   c.at(4).get<int>(),      c.at(5).get<std::vector<int>>()};
        if (c.size() != 6 || !valid(cell.seed, cell.act, cell.region, cell.x, cell.y) || cell.cels.empty() ||
            cell.cels.size() > 64 || !cells.emplace(cell.act, cell.region, cell.x, cell.y).second ||
            std::any_of(cell.cels.begin(), cell.cels.end(), [](int cel) { return cel < 0 || cel > 65535; }))
            throw std::runtime_error("Invalid remembered automap cel");
        d.cells.push_back(std::move(cell));
    }
    std::set<std::pair<int, int>> towns;
    for (const auto &t : j.at("towns")) {
        AutomapRememberedTown town{t.at(0).get<uint32_t>(), t.at(1).get<int>(), t.at(2).get<int>(),
                                   t.at(3).get<int>(),      t.at(4).get<int>(), t.at(5).get<int>()};
        if (t.size() != 6 || !valid(town.seed, town.act, town.region, town.x, town.y) || town.variant < 0 ||
            !towns.emplace(town.act, town.region).second)
            throw std::runtime_error("Invalid remembered automap town");
        d.towns.push_back(town);
    }
    return d;
}
Json read(const std::filesystem::path &path, const std::string &name, const std::string &characterClass) {
    if (!std::filesystem::exists(path))
        return Json{{"version", 2},
                    {"rules", rules},
                    {"name", name},
                    {"class", characterClass},
                    {"worlds", Json::array()}};
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)) ||
        !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > maxBytes)
        throw std::runtime_error("Automap sidecar is not a bounded regular file");
    std::ifstream file(path, std::ios::binary);
    // Parse the entire file: a valid JSON prefix followed by corrupt bytes is
    // not a valid cache and must never be silently replaced on the next save.
    Json j;
    try { j = Json::parse(file); }
    catch(const Json::parse_error &) { throw std::runtime_error("Malformed automap sidecar; file preserved"); }
    if (j.value("version", 0) != 2 || !j.contains("checksum"))
        throw std::runtime_error("Unsupported automap sidecar version; file preserved");
    const auto expected = j.at("checksum").get<uint64_t>();
    j.erase("checksum");
    if (j.at("version") != 2 || j.at("rules") != rules || j.at("name") != name ||
        j.at("class") != characterClass || !j.at("worlds").is_array() || j.at("worlds").size() > 7 ||
        checksum(j) != expected)
        throw std::runtime_error("Automap sidecar identity, version or checksum mismatch; file preserved");
    for (const auto &world : j.at("worlds"))
        (void)decode(world);
    return j;
}
bool matches(const AutomapDiscovery &a, const AutomapDiscovery &b) {
    if (a.difficulty != b.difficulty || a.contentFingerprint != b.contentFingerprint)
        return false;
    bool overlap = false;
    for (const auto &[act, seed] : a.actSeeds)
        if (b.actSeeds.contains(act)) {
            if (b.actSeeds.at(act) != seed)
                return false;
            overlap = true;
        }
    return overlap;
}
} // namespace
AutomapStore::AutomapStore(std::filesystem::path character, std::string name, std::string characterClass)
    : path_(std::move(character)), name_(std::move(name)), characterClass_(std::move(characterClass)) {
    path_.replace_extension(".d2xmap");
}
std::optional<AutomapDiscovery> AutomapStore::load(bool tcpIp, int difficulty, int act, uint32_t seed,
                                                   uint64_t content) const {
    auto j = read(path_, name_, characterClass_);
    AutomapDiscovery requested;
    requested.difficulty = difficulty;
    requested.contentFingerprint = content;
    requested.actSeeds[act] = seed;
    for (auto it = j["worlds"].rbegin(); it != j["worlds"].rend(); ++it)
        if (it->at("tcpIp") == tcpIp) {
            auto d = decode(*it);
            if (matches(d, requested))
                return d;
        }
    return {};
}
void AutomapStore::save(bool tcpIp, const AutomapDiscovery &d) const {
    if (d.actSeeds.empty() || d.layers.empty())
        return;
    auto j = read(path_, name_, characterClass_);
    auto &worlds = j["worlds"];
    const auto value = encode(d, tcpIp);
    (void)decode(value);
    for (auto it = worlds.begin(); it != worlds.end();) {
        const bool sameMode = it->at("tcpIp") == tcpIp;
        if (sameMode && ((!tcpIp && it->at("difficulty") == d.difficulty) || matches(decode(*it), d)))
            it = worlds.erase(it);
        else
            ++it;
    }
    worlds.push_back(value);
    const size_t limit = tcpIp ? 4 : 3;
    while (size_t(std::count_if(worlds.begin(), worlds.end(),
                                [&](const auto &v) { return v.at("tcpIp") == tcpIp; })) > limit)
        worlds.erase(std::find_if(worlds.begin(), worlds.end(),
                                  [&](const auto &v) { return v.at("tcpIp") == tcpIp; }));
    j["checksum"] = checksum(j);
    const auto bytes = j.dump();
    if (bytes.size() > maxBytes)
        throw std::runtime_error("Automap sidecar exceeds size limit; old file preserved");
    writeFileAtomically(path_, std::span(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()),
                        true);
}
} // namespace d2x::hosting
