#include "native_save.hpp"
#include "core/native_bits.hpp"
#include "gameplay/items/pd2/save_encoding.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>

namespace d2x {
namespace {
constexpr uint32_t compact = 0x00200000;
constexpr uint32_t earFlag = 0x00010000;
void require(bool value, const char *reason) {
    if (!value) throw std::runtime_error(std::string("Unsupported or invalid D2S item: ") + reason);
}
NativeItemSaveRules::StatFormat format(const NativeItemSaveRules &content, unsigned id, const D2sItem &item) {
    const size_t profile=!content.pd2Host || (item.flags&compact) || item.format>=104?0:item.format==103?2:1;
    const auto found=content.stats[profile].find(id);
    require(found!=content.stats[profile].end(),"unknown stat ID");
    require((content.pd2Host || found->second.bits>0) && found->second.bits<=32 && found->second.params<=32,"stat bit widths");
    return found->second;
}
unsigned followers(unsigned id) {
    switch (id) {
    case 17: case 48: case 50: case 52: return 1;
    case 54: case 57: return 2;
    default: return 0;
    }
}
int64_t readValue(D2sBitReader &bits, const NativeItemSaveRules &content, unsigned id, const D2sItem &item) {
    const auto rule = format(content, id, item);
    return int64_t(bits.read(rule.bits)) - rule.add;
}
void writeValue(D2sBitWriter &bits, const NativeItemSaveRules &content, unsigned id, int64_t value, const D2sItem &item) {
    const auto rule = format(content, id, item);
    if(content.pd2Host) {
        bits.write(pd2ItemSavedValue(value,rule.add,rule.bits),rule.bits);
        return;
    }
    value += rule.add;
    require(value >= 0 && value <= UINT32_MAX, "stat range");
    bits.write(uint32_t(value), rule.bits);
}
void validate(const D2sItem &item, const ItemDefinition &definition, const NativeItemSaveRules &content,bool ground) {
    require(item.code.size() >= 1 && item.code.size() <= 4, "base code");
    require((item.flags & 0x2000000u) == 0, "transient gamble item");
    require(item.mode == 0 || item.mode == 1 || item.mode == 2 || item.mode == 4 || item.mode == 6 || (ground && (item.mode==3 || item.mode==5)), "location");
    require(bool(item.flags & compact) == content.bases.at(item.code).compact, "compact flag");
    require(item.quality >= 1 && item.quality <= 9, "quality");
    require(item.level >= 1 && item.level <= 127, "item level");
    require(bool(item.flags & earFlag) == bool(item.ear) &&
        bool(item.ear) == definition.equipment.isType("play"), "ear flag/base/identity");
    if (item.ear) {
        require(item.ear->characterClass < 7 && item.ear->level < 128 &&
            !item.ear->name.empty() && item.ear->name.size() <= 15, "ear identity range");
        require(std::all_of(item.ear->name.begin(), item.ear->name.end(), [](unsigned char c) {
            return c > 0 && c < 128;
        }), "ear name character");
    }
    if (item.quality == 9) require(std::all_of(item.prefixes.begin(), item.prefixes.end(), [](auto v) { return !v; }) &&
        std::all_of(item.suffixes.begin(), item.suffixes.end(), [](auto v) { return !v; }), "tempered affix identity");
    if(item.autoAffix) {
        require(item.autoAffix<=content.autoAffixGroups.size() && item.autoAffix<2048,"automatic affix row");
        require(content.autoAffixGroups[item.autoAffix-1]==content.bases.at(item.code).autoPrefix,"automatic affix/base group");
    }
}
void validateAddedProperties(const D2sItem &item, const ItemDefinition &definition) {
    require(!(item.flags & compact) || (!(item.flags & (0x4000000u | 0x1000000u)) &&
        item.stats.empty() && item.runewordStats.empty() &&
        std::all_of(item.setStats.begin(), item.setStats.end(), [](const auto &v) { return v.empty(); })),
        "properties on compact item");
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
std::string readName(D2sBitReader &bits) {
    std::string name;
    for (unsigned i = 0; i < 16; ++i) {
        const auto character = bits.read(7);
        if (!character) return name;
        require(i < 15, "name length"); name.push_back(char(character));
    }
    throw std::runtime_error("Unterminated native item name");
}
void writeName(D2sBitWriter &bits, const std::string &name) {
    require(!name.empty() && name.size() <= 15, "name length");
    for (unsigned char character : name) {
        require(character > 0 && character < 128, "name character"); bits.write(character, 7);
    }
    bits.write(0, 7);
}
void readEar(D2sBitReader &bits, D2sItem &item) {
    ItemEarIdentity identity;
    identity.characterClass = bits.read(3); identity.level = bits.read(7);
    identity.name = readName(bits); item.ear = std::move(identity);
}
void writeEar(D2sBitWriter &bits, const D2sItem &item) {
    bits.write(item.ear->characterClass, 3); bits.write(item.ear->level, 7);
    writeName(bits, item.ear->name);
}
void readStats(D2sBitReader &bits, const NativeItemSaveRules &content, D2sItem &item) {
    const unsigned width = itemSaveEncoding(content.pd2Host,item.format,false).statBits;
    const unsigned end = (1u << width) - 1;
    for (size_t count = 0; count <= end; ++count) {
        const auto id = bits.read(width);
        if (id == end) return;
        for (unsigned next = 0; next <= followers(id); ++next) {
            const auto rule = format(content, id + next, item);
            const auto parameter = bits.read(rule.params);
            item.stats.push_back({uint16_t(id + next), readValue(bits, content, id + next, item), int32_t(parameter)});
        }
    }
    throw std::runtime_error("D2S item stat list exceeds limit");
}
void writeStats(D2sBitWriter &bits, const NativeItemSaveRules &content, const D2sItem &item) {
    const unsigned width = itemSaveEncoding(content.pd2Host,item.format,false).statBits;
    const unsigned end = (1u << width) - 1;
    std::map<std::pair<unsigned, int32_t>, int64_t> values;
    for (const auto &stat : item.stats) {
        require(stat.id < end && stat.parameter >= 0, "stat identity");
        require(values.emplace(std::pair{unsigned(stat.id), stat.parameter}, stat.value).second, "duplicate stat");
    }
    while (!values.empty()) {
        auto [key, value] = *values.begin();
        unsigned id = key.first;
        if (id == 18 || id == 49 || id == 51 || id == 53 || id == 55 || id == 58) --id;
        else if (id == 56 || id == 59) id -= 2;
        bits.write(id, width);
        for (unsigned next = 0; next <= followers(id); ++next) {
            const auto rule = format(content, id + next, item);
            auto found = values.find({id + next, key.second});
            try {
                bits.write(content.pd2Host?pd2ItemSavedValue(key.second,0,rule.params):uint32_t(key.second), rule.params);
                writeValue(bits, content, id + next, found == values.end() ? 0 : found->second, item);
            } catch(const std::exception &error) {
                throw std::runtime_error("D2S item stat "+std::to_string(id+next)+" layer "+std::to_string(key.second)+": "+error.what());
            }
            if (found != values.end()) values.erase(found);
        }
    }
    bits.write(end, width);
}
} // namespace
D2sItemRead readNativeSavedItem(std::span<const uint8_t> bytes, const NativeItemSaveRules &content,bool ground) {
    D2sBitReader bits(bytes);
    require(bits.read(16) == 0x4D4A, "JM signature");
    D2sItem item;
    item.flags = bits.read(32);
    item.format = bits.read(10);
    item.mode = bits.read(3);
    require(item.mode <= 2 || item.mode == 4 || item.mode == 6 || (ground && (item.mode==3 || item.mode==5)), "ground item");
    if(item.mode==3 || item.mode==5) {item.x=bits.read(16);item.y=bits.read(16);item.page=0;}
    else {item.body=bits.read(4);item.x=bits.read(4);item.y=bits.read(4);item.page=bits.read(3);}
    if ((item.flags & (compact | earFlag)) == (compact | earFlag)) {
        item.code = "ear"; readEar(bits, item);
    } else {
        const auto code = bits.read(32);
        for (unsigned index = 0; index < 4; ++index) item.code.push_back(char(code >> (index * 8)));
        while (!item.code.empty() && (item.code.back() == ' ' || !item.code.back())) item.code.pop_back();
    }
    const auto base=content.bases.find(item.code);
    require(base!=content.bases.end(), "unknown base item");
    const auto *definition=&base->second.definition;
    validate(item, *definition, content,ground);
    unsigned children = 0;
    if (item.flags & compact) {
        require(ground || item.code != "gld", "loose gold in character inventory");
        if(item.code=="gld") {const auto wide=bits.read(1);item.quantity=bits.read(wide?32:12);}
        if (base->second.questDifficulty)
            item.questDifficulty = unsigned(readValue(bits, content, 356, item));
    } else {
        children = bits.read(3);
        item.seed = bits.read(32);
        item.level = bits.read(7);
        item.quality = bits.read(4);
        item.hasGraphic = bits.read(1) != 0;
        if (item.hasGraphic) item.graphic = bits.read(3);
        if (bits.read(1)) item.autoAffix = bits.read(11);
        validate(item, *definition, content,ground);
        switch (item.quality) {
        case 1: case 3: item.fileIndex = bits.read(3); break;
        case 2:
            if (definition->equipment.isType("body") && !definition->equipment.isType("play")) item.fileIndex = bits.read(10);
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
        case 9: item.rarePrefix = bits.read(8); item.rareSuffix = bits.read(8); break;
        }
    }
    if (item.flags & 0x4000000u) item.runewordId = bits.read(16);
    if (!(item.flags & compact) && (item.flags & earFlag)) readEar(bits, item);
    else if (item.flags & 0x1000000u) item.personalizedName = readName(bits);
    if (bits.read(1)) {
        ItemRealmIdentity identity;
        for (auto &word : identity) word = bits.read(32);
        item.realmIdentity = identity;
    }
    if (!(item.flags & compact)) {
        if (definition->family == ItemFamily::Armor) item.defense = unsigned(readValue(bits, content, 31, item));
        if (definition->family != ItemFamily::Misc) {
            item.maxDurability = unsigned(readValue(bits, content, 73, item));
            if (item.maxDurability) item.durability = unsigned(readValue(bits, content, 72, item));
        }
        if (base->second.stackable) item.quantity = bits.read(9);
        if (item.flags & 0x800u) item.sockets = unsigned(readValue(bits, content, 194, item));
        const auto setMask = item.quality == 5 ? bits.read(5) : 0;
        readStats(bits, content, item);
        for (size_t index = 0; index < item.setStats.size(); ++index)
            if (setMask & (1u << index)) {
                D2sItem bonus; bonus.format=item.format;
                readStats(bits, content, bonus);
                item.setStats[index] = std::move(bonus.stats);
            }
        if (item.flags & 0x4000000u) {
            D2sItem bonus; bonus.format=item.format; readStats(bits, content, bonus);
            item.runewordStats = std::move(bonus.stats);
        }
    }
    validate(item, *definition, content,ground);
    validateAddedProperties(item, *definition);
    require(children <= item.sockets && children <= 6 && (!children || item.mode != 6), "socket child count");
    size_t consumed = bits.size();
    for (unsigned index = 0; index < children; ++index) {
        auto child = readNativeSavedItem(bytes.subspan(consumed), content,ground);
        require(child.item.mode == 6 && child.item.x == index && child.item.y == 0, "socket child mode/order");
        consumed += child.bytesRead;
        item.socketedItems.push_back(std::move(child.item));
    }
    return {std::move(item), consumed};
}
Bytes writeNativeSavedItem(const D2sItem &item, const NativeItemSaveRules &content,bool ground) {
    const auto base=content.bases.find(item.code);
    require(base!=content.bases.end(), "unknown base item");
    const auto *definition=&base->second.definition;
    validate(item, *definition, content,ground);
    D2sBitWriter bits;
    validateAddedProperties(item, *definition);
    bits.write(0x4D4A, 16); bits.write(item.flags, 32); bits.write(item.format, 10);
    bits.write(item.mode, 3);
    if(item.mode==3 || item.mode==5) {bits.write(item.x,16);bits.write(item.y,16);}
    else {bits.write(item.body,4);bits.write(item.x,4);bits.write(item.y,4);bits.write(item.page,3);}
    uint32_t code = 0;
    for (unsigned index = 0; index < 4; ++index)
        code |= uint32_t(index < item.code.size() ? uint8_t(item.code[index]) : uint8_t(' ')) << (index * 8);
    if ((item.flags & (compact | earFlag)) == (compact | earFlag)) writeEar(bits, item);
    else bits.write(code, 32);
    if (item.flags & compact) {
        require(item.stats.empty() && item.quality == 2, "compact properties");
        if(item.code=="gld") {require(ground,"loose gold in character inventory");bits.write(item.quantity>=4096,1);bits.write(item.quantity,item.quantity>=4096?32:12);}
        if (base->second.questDifficulty)
            writeValue(bits, content, 356, item.questDifficulty, item);
    } else {
        bits.write(unsigned(item.socketedItems.size()), 3); bits.write(item.seed, 32); bits.write(item.level, 7); bits.write(item.quality, 4);
        bits.write(item.hasGraphic, 1);
        if (item.hasGraphic) bits.write(item.graphic, 3);
        bits.write(item.autoAffix != 0, 1); if(item.autoAffix) bits.write(item.autoAffix,11);
        switch (item.quality) {
        case 1: case 3: bits.write(item.fileIndex, 3); break;
        case 2:
            if (definition->equipment.isType("body") && !definition->equipment.isType("play")) bits.write(item.fileIndex, 10);
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
        case 9: bits.write(item.rarePrefix, 8); bits.write(item.rareSuffix, 8); break;
        }
    }
    if (item.flags & 0x4000000u) bits.write(item.runewordId, 16);
    if (!(item.flags & compact) && (item.flags & earFlag)) writeEar(bits, item);
    else if (item.flags & 0x1000000u) writeName(bits, item.personalizedName);
    bits.write(item.realmIdentity.has_value(), 1);
    if (item.realmIdentity) for (const auto word : *item.realmIdentity) bits.write(word, 32);
    if (!(item.flags & compact)) {
        if (definition->family == ItemFamily::Armor) writeValue(bits, content, 31, item.defense, item);
        if (definition->family != ItemFamily::Misc) {
            writeValue(bits, content, 73, item.maxDurability, item);
            if (item.maxDurability) writeValue(bits, content, 72, item.durability, item);
        }
        if (base->second.stackable) bits.write(item.quantity, 9);
        if (item.flags & 0x800u) writeValue(bits, content, 194, item.sockets, item);
        unsigned setMask = 0;
        for (size_t index = 0; index < item.setStats.size(); ++index)
            if (!item.setStats[index].empty()) setMask |= 1u << index;
        require(!setMask || item.quality == 5, "set attributes on non-set item");
        if (item.quality == 5) bits.write(setMask, 5);
        writeStats(bits, content, item);
        for (const auto &stats : item.setStats)
            if (!stats.empty()) {
                D2sItem bonus; bonus.format=item.format;
                bonus.stats = stats;
                writeStats(bits, content, bonus);
            }
        if (item.flags & 0x4000000u) {
            D2sItem bonus; bonus.format=item.format; bonus.stats = item.runewordStats;
            writeStats(bits, content, bonus);
        }
    }
    auto bytes = bits.bytes();
    for (size_t index = 0; index < item.socketedItems.size(); ++index) {
        const auto &child = item.socketedItems[index];
        require(child.mode == 6 && child.x == index && child.y == 0, "socket child mode/order");
        auto encoded = writeNativeSavedItem(child, content,ground);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }
    return bytes;
}
} // namespace d2x
