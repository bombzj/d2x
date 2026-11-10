#include "pd2_split_save.hpp"
#include "d2s_header.hpp"
#include "d2s_items.hpp"
#include "d2s_skills.hpp"
#include "d2s_stats.hpp"
#include "save_codec.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace d2x {
namespace {
constexpr size_t fixedEnd = d2sHeaderSize + 298 + 80 + 52;
constexpr uint16_t jm = 0x4d4a;
constexpr uint16_t bankGold = 15;
using View = std::span<const uint8_t>;

void require(bool condition, const char *reason) {
    if (!condition) throw std::runtime_error(std::string("PD2 split save: ") + reason);
}
void append(Bytes &out, View bytes) { out.insert(out.end(), bytes.begin(), bytes.end()); }
uint16_t word(View bytes, size_t at) {
    require(at <= bytes.size() && bytes.size() - at >= 2, "truncated section");
    return uint16_t(bytes[at] | unsigned(bytes[at + 1]) << 8);
}
uint32_t dword(View bytes, size_t at) { return word(bytes, at) | uint32_t(word(bytes, at + 2)) << 16; }
void word(Bytes &out, uint16_t value) {
    out.push_back(uint8_t(value));
    out.push_back(uint8_t(value >> 8));
}
void dword(Bytes &out, uint32_t value) {
    word(out, uint16_t(value));
    word(out, uint16_t(value >> 16));
}

struct Root { View bytes; bool stash{}; };
struct List { size_t first{}, end{}; std::vector<Root> roots; };
List list(View bytes, size_t at, const ClassicData &content) {
    require(word(bytes, at) == jm, "missing JM item list");
    const auto count = word(bytes, at + 2);
    require(count <= 1024, "too many item roots");
    List result{at, at + 4, {}};
    for (unsigned index = 0; index < count; ++index) {
        try {
            const auto decoded = readD2sItem(bytes.subspan(result.end), content);
            result.roots.push_back({bytes.subspan(result.end, decoded.bytesRead), decoded.item.mode == 0 && decoded.item.page == 5});
            result.end += decoded.bytesRead;
        } catch (const std::runtime_error &error) {
            throw std::runtime_error("PD2 split save: JM root " + std::to_string(index + 1) +
                " at byte " + std::to_string(result.end) + ": " + error.what());
        }
    }
    return result;
}
View whole(View bytes, const List &items) { return bytes.subspan(items.first, items.end - items.first); }
void writeList(Bytes &out, std::span<const Root> roots) {
    require(roots.size() <= 1024, "too many item roots");
    word(out, jm);
    word(out, uint16_t(roots.size()));
    for (const auto &root : roots) append(out, root.bytes);
}
struct MainLists {
    D2sHeader header;
    D2sStats stats;
    List player, hireling;
    bool hasHireling{};
};
MainLists mainLists(View bytes, const ClassicData &content) {
    MainLists result;
    result.header = readD2sHeader(bytes);
    const auto &header = result.header;
    require(!(header.flags & 1) && bytes.size() >= fixedEnd, "invalid main file body");
    size_t at = fixedEnd;
    result.stats = readD2sStats(bytes.subspan(at), content.tables.at("itemstatcost"));
    at += result.stats.bytesRead;
    at += readD2sSkills(bytes.subspan(at), header.characterClass, header.skillCount, content.tables.at("skills")).bytesRead;
    result.player = list(bytes, at, content);
    at = result.player.end;
    require(word(bytes, at) == jm && word(bytes, at + 2) <= 1, "invalid main corpse section");
    const auto corpses = word(bytes, at + 2);
    at += 4;
    if (corpses) at = list(bytes, at + 12, content).end;
    require(word(bytes, at) == 0x666a, "missing main hireling marker");
    at += 2;
    result.hasHireling = header.mercExperience || header.mercSeed || header.mercName;
    result.hireling = result.hasHireling ? list(bytes, at, content) : List{at, at, {}};
    require(word(bytes, result.hireling.end) == 0x666b, "missing main golem marker");
    return result;
}
struct ExtendedLists {
    View prefix, nn, mm, suffix;
    uint32_t gold{};
    List stash, player, hireling;
};
ExtendedLists extendedLists(View bytes, const ClassicData &content) {
    constexpr std::array<uint8_t, 6> signature{'C','S','T','M','0','1'};
    require(bytes.size() >= 21 && std::equal(signature.begin(), signature.end(), bytes.begin()), "unsupported extended header");
    require(dword(bytes, 10) == 1 && word(bytes, 14) == 0x5453, "only one personal stash page is supported");
    const auto name = bytes.subspan(20, std::min<size_t>(21, bytes.size() - 20));
    const auto end = std::find(name.begin(), name.end(), uint8_t{0});
    require(end != name.end(), "unterminated stash page name");
    const auto stashAt = 21 + size_t(end - name.begin());
    auto stash = list(bytes, stashAt, content);
    require(std::all_of(stash.roots.begin(), stash.roots.end(), [](const Root &root) { return root.stash; }), "non-stash item in ST");
    const auto nnAt = stash.end;
    require(word(bytes, nnAt) == 0x4e4e && dword(bytes, nnAt + 2) == 0, "unsupported NN section");
    auto player = list(bytes, nnAt + 6, content);
    require(std::none_of(player.roots.begin(), player.roots.end(), [](const Root &root) { return root.stash; }), "stash item in NN");
    const auto mmAt = player.end;
    require(word(bytes, mmAt) == 0x4d4d && dword(bytes, mmAt + 2) == 0, "unsupported MM section");
    auto hireling = list(bytes, mmAt + 6, content);
    require(word(bytes, hireling.end) == 0x4a6b, "missing kJ section");
    const auto gold = dword(bytes, hireling.end + 2);
    constexpr std::array<uint8_t, 10> emptySuffix{'J','J',0,0,0,0,'J','M',0,0};
    const auto suffix = bytes.subspan(hireling.end + 6);
    require(std::equal(suffix.begin(), suffix.end(), emptySuffix.begin(), emptySuffix.end()), "unsupported JJ data");
    return {bytes.first(stashAt), bytes.subspan(nnAt, 6), bytes.subspan(mmAt, 6), suffix, gold,
        std::move(stash), std::move(player), std::move(hireling)};
}
} // namespace

Bytes combinePd2Save(const CharacterSaveFiles &files, const ClassicData &content) {
    if (!files.extended) return files.main;
    require(content.pd2Host, "extended data requires PD2 rules");
    const View main = files.main, extended = *files.extended;
    auto layout = mainLists(main, content);
    const auto extra = extendedLists(extended, content);
    require(layout.player.roots.empty() && layout.hireling.roots.empty(), "main and extended files both contain carried items");
    require(layout.hasHireling || extra.hireling.roots.empty(), "extended hireling items have no main-file owner");
    auto player = extra.player.roots;
    player.insert(player.end(), extra.stash.roots.begin(), extra.stash.roots.end());
    Bytes bytes;
    if (extra.gold) {
        auto &stats = layout.stats.values;
        const auto bank = std::find_if(stats.begin(), stats.end(), [](const auto &stat) { return stat.id == bankGold; });
        if (bank == stats.end()) stats.push_back({bankGold, extra.gold});
        else {
            require(bank->value == 0 || bank->value == extra.gold, "conflicting bank gold");
            bank->value = extra.gold;
        }
        append(bytes, main.first(fixedEnd));
        writeD2sStats(bytes, stats, content.tables.at("itemstatcost"));
        append(bytes, main.subspan(fixedEnd + layout.stats.bytesRead, layout.player.first - fixedEnd - layout.stats.bytesRead));
    } else append(bytes, main.first(layout.player.first));
    writeList(bytes, player);
    append(bytes, main.subspan(layout.player.end, layout.hireling.first - layout.player.end));
    if (layout.hasHireling) append(bytes, whole(extended, extra.hireling));
    append(bytes, main.subspan(layout.hireling.end));
    require(bytes.size() <= maxSaveBytes, "combined file exceeds supported size");
    writeD2sHeader(bytes, layout.header);
    return bytes;
}

CharacterSaveFiles splitPd2Save(View complete, View extendedLayout, const ClassicData &content) {
    auto layout = mainLists(complete, content);
    const auto extra = extendedLists(extendedLayout, content);
    std::vector<Root> stash, player;
    std::vector<unsigned> nnSlots;
    for (const auto &root : layout.player.roots) {
        if (root.stash) stash.push_back(root);
        else player.push_back(root);
        nnSlots.push_back(root.stash ? 0 : unsigned(player.size()));
    }
    const auto remap = [&](uint32_t &skill) {
        const unsigned source = skill >> 16;
        if (!source || source == 0xffff) return;
        require(source <= nnSlots.size() && nnSlots[source - 1], "skill source is outside the carried items");
        skill = (skill & 0xffff) | uint32_t(nnSlots[source - 1]) << 16;
    };
    for (auto &skill : layout.header.hotkeys) remap(skill);
    for (auto &skill : layout.header.selectedSkills) remap(skill);
    CharacterSaveFiles result;
    append(result.main, complete.first(layout.player.first));
    writeList(result.main, {});
    append(result.main, complete.subspan(layout.player.end, layout.hireling.first - layout.player.end));
    if (layout.hasHireling) writeList(result.main, {});
    append(result.main, complete.subspan(layout.hireling.end));
    writeD2sHeader(result.main, layout.header);

    auto &extended = result.extended.emplace();
    append(extended, extra.prefix);
    writeList(extended, stash);
    append(extended, extra.nn);
    writeList(extended, player);
    append(extended, extra.mm);
    writeList(extended, layout.hireling.roots);
    const auto &stats = layout.stats.values;
    const auto bank = std::find_if(stats.begin(), stats.end(), [](const auto &stat) { return stat.id == bankGold; });
    require(bank == stats.end() || (bank->value >= 0 && bank->value <= UINT32_MAX), "invalid bank gold");
    word(extended, 0x4a6b);
    dword(extended, bank == stats.end() ? 0 : uint32_t(bank->value));
    append(extended, extra.suffix);
    require(extended.size() <= maxSaveBytes, "extended file exceeds supported size");
    return result;
}
} // namespace d2x
