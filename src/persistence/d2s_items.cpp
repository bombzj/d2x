#include "d2s_items.hpp"
#include "d2s_bits.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>

namespace d2x {
namespace {
constexpr uint32_t compact = 0x00200000;
void require(bool value, const char *reason) {
    if (!value) throw std::runtime_error(std::string("Unsupported or invalid D2S item: ") + reason);
}
struct StatFormat { unsigned bits, params; int add; };
StatFormat format(const ClassicData &content, unsigned id) {
    const auto &table = content.tables.at("itemstatcost");
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (table.number(row, "ID") == int(id)) {
            const int bits = table.number(row, "Save Bits").value_or(0);
            const int params = table.number(row, "Save Param Bits").value_or(0);
            require(bits > 0 && bits <= 32 && params >= 0 && params <= 32, "stat bit widths");
            return {unsigned(bits), unsigned(params), table.number(row, "Save Add").value_or(0)};
        }
    throw std::runtime_error("Unknown D2S item stat ID");
}
unsigned followers(unsigned id) {
    switch (id) {
    case 17: case 48: case 50: case 52: return 1;
    case 54: case 57: return 2;
    default: return 0;
    }
}
int64_t readValue(D2sBitReader &bits, const ClassicData &content, unsigned id) {
    const auto rule = format(content, id);
    return int64_t(bits.read(rule.bits)) - rule.add;
}
void writeValue(D2sBitWriter &bits, const ClassicData &content, unsigned id, int64_t value) {
    const auto rule = format(content, id);
    value += rule.add;
    require(value >= 0 && value <= UINT32_MAX, "stat range");
    bits.write(uint32_t(value), rule.bits);
}
bool flag(const ClassicData &content, const ItemDefinition &item, const char *column) {
    return content.tables.at(item.base.sourceTable).number(item.base.sourceRow, column).value_or(0) != 0;
}
void validate(const D2sItem &item, const ItemDefinition &definition, const ClassicData &content) {
    require(item.code.size() >= 1 && item.code.size() <= 4, "base code");
    require((item.flags & (0x2000000u | 0x10000u)) == 0, "ear or gamble item");
    require(item.mode == 0 || item.mode == 1 || item.mode == 2 || item.mode == 4 || item.mode == 6, "location");
    require(bool(item.flags & compact) == flag(content, definition, "compactsave"), "compact flag");
    require(item.quality >= 1 && item.quality <= 8, "quality");
    require(item.level >= 1 && item.level <= 99, "item level");
    require(item.autoAffix == 0, "automatic affix");
}
void validateAddedProperties(const D2sItem &item, const ItemDefinition &definition) {
    require(bool(item.flags & 0x800u) == bool(item.sockets), "socket flag/count mismatch");
    require(item.sockets <= unsigned(std::max(0, definition.base.sockets.value_or(0))) &&
        (!item.sockets || !(item.flags & compact)), "socket count or compact sockets");
    require(item.socketedItems.size() <= item.sockets && item.socketedItems.size() <= 6, "socket children count");
    require(item.mode != 6 || (definition.equipment.isType("sock") && item.socketedItems.empty() &&
        !item.sockets && (item.flags & 0x10u)), "invalid socket child");
    require(bool(item.flags & 0x4000000u) == bool(item.runewordId) &&
        ((item.flags & 0x4000000u) || item.runewordStats.empty()), "runeword flag/identity");
    require(bool(item.flags & 0x1000000u) == !item.personalizedName.empty(), "personalization flag/name mismatch");
    require(item.personalizedName.size() <= 15 &&
        (item.personalizedName.empty() || (definition.personalizable && !(item.flags & compact))), "personalized item type/name");
}
void readStats(D2sBitReader &bits, const ClassicData &content, D2sItem &item) {
    for (size_t count = 0; count < 512; ++count) {
        const auto id = bits.read(9);
        if (id == 511) return;
        for (unsigned next = 0; next <= followers(id); ++next) {
            const auto rule = format(content, id + next);
            const auto parameter = bits.read(rule.params);
            item.stats.push_back({uint16_t(id + next), readValue(bits, content, id + next), int32_t(parameter)});
        }
    }
    throw std::runtime_error("D2S item stat list exceeds limit");
}
void writeStats(D2sBitWriter &bits, const ClassicData &content, const D2sItem &item) {
    std::map<std::pair<unsigned, int32_t>, int64_t> values;
    for (const auto &stat : item.stats) {
        require(stat.id < 511 && stat.parameter >= 0, "stat identity");
        require(values.emplace(std::pair{unsigned(stat.id), stat.parameter}, stat.value).second, "duplicate stat");
    }
    while (!values.empty()) {
        auto [key, value] = *values.begin();
        unsigned id = key.first;
        if (id == 18 || id == 49 || id == 51 || id == 53 || id == 55 || id == 58) --id;
        else if (id == 56 || id == 59) id -= 2;
        bits.write(id, 9);
        for (unsigned next = 0; next <= followers(id); ++next) {
            const auto rule = format(content, id + next);
            auto found = values.find({id + next, key.second});
            bits.write(uint32_t(key.second), rule.params);
            writeValue(bits, content, id + next, found == values.end() ? 0 : found->second);
            if (found != values.end()) values.erase(found);
        }
    }
    bits.write(511, 9);
}
} // namespace
D2sItemRead readD2sItem(std::span<const uint8_t> bytes, const ClassicData &content) {
    D2sBitReader bits(bytes);
    require(bits.read(16) == 0x4D4A, "JM signature");
    D2sItem item;
    item.flags = bits.read(32);
    item.format = bits.read(10);
    item.mode = bits.read(3);
    require(item.mode <= 2 || item.mode == 4 || item.mode == 6, "ground item");
    item.body = bits.read(4);
    item.x = bits.read(4);
    item.y = bits.read(4);
    item.page = bits.read(3);
    const auto code = bits.read(32);
    for (unsigned index = 0; index < 4; ++index) item.code.push_back(char(code >> (index * 8)));
    while (!item.code.empty() && (item.code.back() == ' ' || !item.code.back())) item.code.pop_back();
    const auto *definition = content.items.find(item.code);
    require(definition != nullptr, "unknown base item");
    validate(item, *definition, content);
    unsigned children = 0;
    if (item.flags & compact) {
        require(item.code != "gld", "loose gold in character inventory");
        if (flag(content, *definition, "quest") && flag(content, *definition, "questdiffcheck"))
            item.questDifficulty = unsigned(readValue(bits, content, 356));
    } else {
        children = bits.read(3);
        item.seed = bits.read(32);
        item.level = bits.read(7);
        item.quality = bits.read(4);
        item.hasGraphic = bits.read(1) != 0;
        if (item.hasGraphic) item.graphic = bits.read(3);
        if (bits.read(1)) item.autoAffix = bits.read(11);
        validate(item, *definition, content);
        switch (item.quality) {
        case 1: case 3: item.fileIndex = bits.read(3); break;
        case 2:
            require(!definition->equipment.isType("body"), "normal body part");
            if (definition->equipment.isType("char")) {
                const bool prefix = bits.read(1) != 0;
                (prefix ? item.prefixes[0] : item.suffixes[0]) = bits.read(11);
            }
            if (definition->equipment.isType("book") || definition->equipment.isType("scro")) item.book = bits.read(5);
            break;
        case 4: item.prefixes[0] = bits.read(11); item.suffixes[0] = bits.read(11); break;
        case 5: case 7: item.fileIndex = bits.read(12); break;
        case 6: case 8:
            item.rarePrefix = bits.read(8); item.rareSuffix = bits.read(8);
            for (size_t index = 0; index < 3; ++index) {
                if (bits.read(1)) item.prefixes[index] = bits.read(11);
                if (bits.read(1)) item.suffixes[index] = bits.read(11);
            }
            break;
        }
    }
    if (item.flags & 0x4000000u) item.runewordId = bits.read(16);
    if (item.flags & 0x1000000u) {
        for (unsigned i = 0; i < 16; ++i) {
            const auto character = bits.read(7);
            if (!character) break;
            require(i < 15, "personalized name length"); item.personalizedName.push_back(char(character));
        }
        require(!item.personalizedName.empty(), "empty personalized name");
    }
    require(bits.read(1) == 0, "realm item data");
    if (!(item.flags & compact)) {
        if (definition->family == ItemFamily::Armor) item.defense = unsigned(readValue(bits, content, 31));
        if (definition->family != ItemFamily::Misc) {
            item.maxDurability = unsigned(readValue(bits, content, 73));
            if (item.maxDurability) item.durability = unsigned(readValue(bits, content, 72));
        }
        if (flag(content, *definition, "stackable")) item.quantity = bits.read(9);
        if (item.flags & 0x800u) item.sockets = unsigned(readValue(bits, content, 194));
        const auto setMask = item.quality == 5 ? bits.read(5) : 0;
        readStats(bits, content, item);
        for (size_t index = 0; index < item.setStats.size(); ++index)
            if (setMask & (1u << index)) {
                D2sItem bonus;
                readStats(bits, content, bonus);
                item.setStats[index] = std::move(bonus.stats);
            }
        if (item.flags & 0x4000000u) {
            D2sItem bonus; readStats(bits, content, bonus);
            item.runewordStats = std::move(bonus.stats);
        }
    }
    validateAddedProperties(item, *definition);
    require(children <= item.sockets && children <= 6 && (!children || item.mode != 6), "socket child count");
    size_t consumed = bits.size();
    for (unsigned index = 0; index < children; ++index) {
        auto child = readD2sItem(bytes.subspan(consumed), content);
        require(child.item.mode == 6 && child.item.x == index && child.item.y == 0, "socket child mode/order");
        consumed += child.bytesRead;
        item.socketedItems.push_back(std::move(child.item));
    }
    return {std::move(item), consumed};
}
Bytes writeD2sItem(const D2sItem &item, const ClassicData &content) {
    const auto *definition = content.items.find(item.code);
    require(definition != nullptr, "unknown base item");
    validate(item, *definition, content);
    D2sBitWriter bits;
    validateAddedProperties(item, *definition);
    bits.write(0x4D4A, 16); bits.write(item.flags, 32); bits.write(item.format, 10);
    bits.write(item.mode, 3); bits.write(item.body, 4); bits.write(item.x, 4); bits.write(item.y, 4); bits.write(item.page, 3);
    uint32_t code = 0;
    for (unsigned index = 0; index < 4; ++index)
        code |= uint32_t(index < item.code.size() ? uint8_t(item.code[index]) : uint8_t(' ')) << (index * 8);
    bits.write(code, 32);
    if (item.flags & compact) {
        require(item.stats.empty() && item.quality == 2, "compact properties");
        if (flag(content, *definition, "quest") && flag(content, *definition, "questdiffcheck"))
            writeValue(bits, content, 356, item.questDifficulty);
    } else {
        bits.write(unsigned(item.socketedItems.size()), 3); bits.write(item.seed, 32); bits.write(item.level, 7); bits.write(item.quality, 4);
        bits.write(item.hasGraphic, 1);
        if (item.hasGraphic) bits.write(item.graphic, 3);
        bits.write(0, 1);
        switch (item.quality) {
        case 1: case 3: bits.write(item.fileIndex, 3); break;
        case 2:
            require(!definition->equipment.isType("body"), "normal body part");
            if (definition->equipment.isType("char")) {
                require(!(item.prefixes[0] && item.suffixes[0]) && !item.prefixes[1] &&
                            !item.prefixes[2] && !item.suffixes[1] && !item.suffixes[2],
                        "normal charm has more than one affix");
                bits.write(item.prefixes[0] != 0, 1);
                bits.write(item.prefixes[0] ? item.prefixes[0] : item.suffixes[0], 11);
            }
            if (definition->equipment.isType("book") || definition->equipment.isType("scro")) bits.write(item.book, 5);
            break;
        case 4: bits.write(item.prefixes[0], 11); bits.write(item.suffixes[0], 11); break;
        case 5: case 7: bits.write(item.fileIndex, 12); break;
        case 6: case 8:
            bits.write(item.rarePrefix, 8); bits.write(item.rareSuffix, 8);
            for (size_t index = 0; index < 3; ++index) {
                bits.write(item.prefixes[index] != 0, 1);
                if (item.prefixes[index]) bits.write(item.prefixes[index], 11);
                bits.write(item.suffixes[index] != 0, 1);
                if (item.suffixes[index]) bits.write(item.suffixes[index], 11);
            }
            break;
        }
    }
    if (item.flags & 0x4000000u) bits.write(item.runewordId, 16);
    if (item.flags & 0x1000000u) {
        require(!item.personalizedName.empty() && item.personalizedName.size() <= 15, "personalized name length");
        for (unsigned char character : item.personalizedName) { require(character > 0 && character < 128, "personalized name character"); bits.write(character, 7); }
        bits.write(0, 7);
    }
    bits.write(0, 1);
    if (!(item.flags & compact)) {
        if (definition->family == ItemFamily::Armor) writeValue(bits, content, 31, item.defense);
        if (definition->family != ItemFamily::Misc) {
            writeValue(bits, content, 73, item.maxDurability);
            if (item.maxDurability) writeValue(bits, content, 72, item.durability);
        }
        if (flag(content, *definition, "stackable")) bits.write(item.quantity, 9);
        if (item.flags & 0x800u) writeValue(bits, content, 194, item.sockets);
        unsigned setMask = 0;
        for (size_t index = 0; index < item.setStats.size(); ++index)
            if (!item.setStats[index].empty()) setMask |= 1u << index;
        require(!setMask || item.quality == 5, "set attributes on non-set item");
        if (item.quality == 5) bits.write(setMask, 5);
        writeStats(bits, content, item);
        for (const auto &stats : item.setStats)
            if (!stats.empty()) {
                D2sItem bonus;
                bonus.stats = stats;
                writeStats(bits, content, bonus);
            }
        if (item.flags & 0x4000000u) {
            D2sItem bonus; bonus.stats = item.runewordStats;
            writeStats(bits, content, bonus);
        }
    }
    auto bytes = bits.bytes();
    for (size_t index = 0; index < item.socketedItems.size(); ++index) {
        const auto &child = item.socketedItems[index];
        require(child.mode == 6 && child.x == index && child.y == 0, "socket child mode/order");
        auto encoded = writeD2sItem(child, content);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }
    return bytes;
}
} // namespace d2x
