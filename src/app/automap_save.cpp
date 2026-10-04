#include "app/automap_save.hpp"
#include "client/automap_exploration.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/session/character_save.hpp"
#include "persistence/save_file.hpp"
#include "presentation/scene_view.hpp"
#include "core/fingerprint.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace d2x {
namespace {
using Json = nlohmann::json;
constexpr size_t maxMapBytes = 16 * 1024 * 1024;
constexpr size_t maxWorldCells = 32 * 1024 * 1024;
constexpr std::string_view explorationRules = "d2x-visible-terrain-v2";
constexpr std::string_view retiredRules = "d2x-near-rooms-v1";
std::filesystem::path mapPath(std::filesystem::path path) {
    path.replace_extension(".d2xmap");
    return path;
}
Json emptyDocument() {
    return Json{{"format", "D2X automap"}, {"version", 1}, {"rules", explorationRules},
                {"worlds", Json::array()}};
}
std::string pack(const std::vector<uint8_t> &seen) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result((seen.size() + 3) / 4, '0');
    for (size_t i = 0; i < result.size(); ++i) {
        unsigned bits = 0;
        for (size_t bit = 0; bit < 4 && i * 4 + bit < seen.size(); ++bit)
            bits |= unsigned(seen[i * 4 + bit]) << bit;
        result[i] = digits[bits];
    }
    return result;
}
AutomapLayers decodeLayers(const Json &world) {
    AutomapLayers layers;
    size_t total = 0;
    const auto &encoded = world.at("layers");
    if (!encoded.is_array() || encoded.size() > 512) throw std::runtime_error("Invalid automap layer count");
    for (const auto &entry : encoded) {
        const int id = entry.at("region").get<int>();
        AutomapLayer layer;
        layer.width = entry.at("width").get<int>(); layer.height = entry.at("height").get<int>();
        layer.layoutFingerprint = entry.at("layout").get<uint64_t>();
        if (id <= 0 || id > 65535 || layer.width <= 0 || layer.height <= 0 ||
            uint64_t(layer.width) * layer.height > maxWorldCells - total)
            throw std::runtime_error("Invalid automap dimensions");
        const size_t cells = size_t(layer.width) * layer.height;
        total += cells;
        const auto bits = entry.at("bits").get<std::string>();
        if (bits.size() != (cells + 3) / 4) throw std::runtime_error("Invalid automap bit count");
        layer.seen.resize(cells);
        for (size_t i = 0; i < bits.size(); ++i) {
            const char c = bits[i];
            const int value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (value < 0 || (i + 1 == bits.size() && cells % 4 && (value >> (cells % 4))))
                throw std::runtime_error("Invalid automap bits");
            for (size_t bit = 0; bit < 4 && i * 4 + bit < cells; ++bit)
                layer.seen[i * 4 + bit] = uint8_t((value >> bit) & 1);
        }
        if (!layers.emplace(RegionId(id), std::move(layer)).second)
            throw std::runtime_error("Duplicate automap layer");
    }
    return layers;
}
void validateWorlds(const Json &document) {
    const auto &worlds = document.at("worlds");
    if (!worlds.is_array() || worlds.size() > 3) throw std::runtime_error("Invalid automap world count");
    std::array<bool, 3> difficulties{};
    for (const auto &world : worlds) {
        const int difficulty = world.at("difficulty").get<int>();
        if (difficulty < 0 || difficulty > 2 || difficulties[size_t(difficulty)])
            throw std::runtime_error("Invalid automap difficulty");
        difficulties[size_t(difficulty)] = true;
        world.at("seed").get<uint32_t>(); world.at("content").get<uint64_t>();
        decodeLayers(world);
    }
}
Json readDocument(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) return emptyDocument();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open automap: " + path.string());
    const auto length = input.tellg();
    if (length <= 0 || uint64_t(length) > maxMapBytes) throw std::runtime_error("Invalid automap file size");
    std::string text(size_t(length), '\0');
    input.seekg(0);
    if (!input.read(text.data(), std::streamsize(text.size()))) throw std::runtime_error("Cannot read automap");
    auto document = Json::parse(text);
    if (document.at("format") != "D2X automap" || document.at("version") != 1 ||
        (document.at("rules") != explorationRules && document.at("rules") != retiredRules))
        throw std::runtime_error("Unsupported automap format/rules; file preserved (no migration)");
    const auto expected = document.at("checksum").get<uint64_t>();
    document.erase("checksum");
    Fingerprint checksum; checksum.add(document.dump());
    if (checksum.value() != expected) throw std::runtime_error("Automap checksum mismatch; file preserved");
    validateWorlds(document);
    return document;
}
bool ownerMatches(const Json &document, const CharacterSaveData &character) {
    return document.value("name", std::string{}) == character.player.name &&
           document.value("class", std::string{}) == character.player.characterClass;
}
} // namespace
void saveLocalGame(const std::filesystem::path &path, const GameSession &session, const SceneView &view) {
    const auto character = session.characterSave();
    auto document = readDocument(mapPath(path)); // Refuse to overwrite unknown/corrupt sidecars.
    if (document.contains("name") && !ownerMatches(document, character))
        throw std::runtime_error("Automap belongs to another character; file preserved");
    if (document.at("rules") == retiredRules) {
        std::cerr << "Automap: retiring whole-room discovery; previous file kept as .d2xmap.bak\n";
        document["worlds"] = Json::array();
        document["rules"] = explorationRules;
    }
    document["name"] = character.player.name; document["class"] = character.player.characterClass;
    Json world{{"difficulty", character.difficulty}, {"seed", character.mapSeed},
               {"content", view.automapContentFingerprint()}, {"layers", Json::array()}};
    for (const auto &[region, layer] : view.automapExploration())
        world["layers"].push_back({{"region", int(region)}, {"width", layer.width}, {"height", layer.height},
                                  {"layout", layer.layoutFingerprint}, {"bits", pack(layer.seen)}});
    auto &worlds = document["worlds"];
    bool replaced = false;
    for (auto &saved : worlds) if (saved.at("difficulty") == character.difficulty) {
        saved = world; replaced = true; break;
    }
    if (!replaced) worlds.push_back(std::move(world));
    validateWorlds(document);
    Fingerprint checksum; checksum.add(document.dump()); document["checksum"] = checksum.value();
    const auto text = document.dump();
    if (text.size() > maxMapBytes) throw std::runtime_error("Automap file too large");
    writeSave(path, character, session.content());
    writeFileAtomically(mapPath(path),
        std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size()), true);
}
bool restoreLocalAutomap(const std::filesystem::path &path, const GameSession &session, SceneView &view) {
    try {
        const auto character = session.characterSave();
        const auto document = readDocument(mapPath(path));
        if (!document.contains("name")) return true; // Native D2S without a D2X discovery file.
        if (!ownerMatches(document, character)) throw std::runtime_error("Automap character mismatch");
        if (document.at("rules") == retiredRules)
            throw std::runtime_error("Whole-room discovery retired; exploring visible terrain afresh. Next save keeps old map as .bak");
        for (const auto &world : document.at("worlds")) {
            if (world.at("difficulty") != character.difficulty) continue;
            if (world.at("seed") != character.mapSeed || world.at("content") != view.automapContentFingerprint())
                throw std::runtime_error("Automap seed/content changed; old discovery not applied");
            return view.restoreAutomapExploration(decodeLayers(world));
        }
    } catch (const std::exception &error) {
        std::cerr << "Automap: " << error.what() << '\n';
        view.notice(std::string("Character loaded; automap not restored: ") + error.what(), true);
        return false;
    }
    return true;
}
} // namespace d2x
