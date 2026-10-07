#include "native_item_wire.hpp"
#include "persistence/d2s_inventory.hpp"
#include "network/protocol/wire.hpp"
#include "network/protocol/bits.hpp"
#include <functional>
#include <map>

namespace d2x {
namespace {
using net::protocol::BitWriter;
struct StatFormat { unsigned bits, params; int add; };
StatFormat format(const ClassicData &data, unsigned id) {
    const auto &table = data.tables.at("itemstatcost");
    for (size_t row = 0; row < table.rows().size(); ++row) if (table.number(row, "ID") == int(id)) {
        const int bits = table.number(row, "Save Bits").value_or(0), params = table.number(row, "Save Param Bits").value_or(0);
        if (bits <= 0 || bits > 32 || params < 0 || params > 32) break;
        return {unsigned(bits), unsigned(params), table.number(row, "Save Add").value_or(0)};
    }
    throw std::runtime_error("Missing native item stat format");
}
void value(BitWriter &bits, const ClassicData &data, unsigned id, int64_t number) {
    const auto rule = format(data, id); number += rule.add;
    if (number < 0 || number > UINT32_MAX) throw std::runtime_error("Native item stat overflow");
    bits.write(uint32_t(number), rule.bits);
}
unsigned followers(unsigned id) {
    return id == 17 || id == 48 || id == 50 || id == 52 ? 1 : id == 54 || id == 57 ? 2 : 0;
}
void stats(BitWriter &bits, const ClassicData &data, const std::vector<D2sStat> &source) {
    std::map<std::pair<unsigned, int32_t>, int64_t> remaining;
    for (const auto &stat : source) {
        if (stat.id >= 511 || stat.parameter < 0 || !remaining.emplace(std::pair{unsigned(stat.id), stat.parameter}, stat.value).second)
            throw std::runtime_error("Invalid native item property");
    }
    while (!remaining.empty()) {
        const auto key = remaining.begin()->first;
        unsigned id = key.first;
        if (id == 18 || id == 49 || id == 51 || id == 53 || id == 55 || id == 58) --id;
        else if (id == 56 || id == 59) id -= 2;
        bits.write(id, 9);
        for (unsigned part = 0; part <= followers(id); ++part) {
            const auto found = remaining.find({id + part, key.second});
            bits.write(uint32_t(key.second), followers(id) ? 0 : format(data, id + part).params);
            value(bits, data, id + part, found == remaining.end() ? 0 : found->second);
            if (found != remaining.end()) remaining.erase(found);
        }
    }
    bits.write(511, 9);
}
Bytes packed(const ClassicData &data, const D2sItem &item) {
    const auto *def = data.items.find(item.code);
    if (!def) throw std::runtime_error("Missing native item definition");
    const auto flag = [&](const char *name) { return data.tables.at(def->base.sourceTable).number(def->base.sourceRow, name).value_or(0) != 0; };
    const bool identified = (item.flags & 0x10) != 0;
    BitWriter bits;
    bits.write(item.flags, 32); bits.write(item.format, 10); bits.write(item.mode, 3);
    bits.write(item.body, 4); bits.write(item.x, 4); bits.write(item.y, 4); bits.write(item.page, 3);
    uint32_t code = 0;
    for (unsigned i = 0; i < 4; ++i) code |= uint32_t(i < item.code.size() ? uint8_t(item.code[i]) : uint8_t(' ')) << (i * 8);
    bits.write(code, 32);
    if (item.flags & 0x200000) {
        if (flag("quest") && flag("questdiffcheck")) value(bits, data, 356, item.questDifficulty);
        return bits.release();
    }
    bits.write(unsigned(item.socketedItems.size()), 3); bits.write(item.level, 7); bits.write(item.quality, 4);
    bits.write(item.hasGraphic, 1); if (item.hasGraphic) bits.write(item.graphic, 3);
    bits.write(item.autoAffix != 0, 1); if (item.autoAffix) bits.write(item.autoAffix, 11);
    switch (item.quality) {
    case 1: case 3: bits.write(item.fileIndex, 3); break;
    case 2:
        if (def->equipment.isType("char") && identified) { bits.write(item.prefixes[0] != 0, 1); bits.write(item.prefixes[0] ? item.prefixes[0] : item.suffixes[0], 11); }
        if (def->equipment.isType("body") && !def->equipment.isType("play")) bits.write(item.fileIndex, 10);
        if (def->equipment.isType("book") || def->equipment.isType("scro")) bits.write(item.book, 5);
        break;
    case 4: if (identified) { bits.write(item.prefixes[0], 11); bits.write(item.suffixes[0], 11); } break;
    case 5: case 7: if (identified) bits.write(item.fileIndex, 12); break;
    case 6: case 8:
        if (identified) { bits.write(item.rarePrefix, 8); bits.write(item.rareSuffix, 8); }
        for (size_t i = 0; i < 3; ++i) {
            bits.write(item.prefixes[i] != 0, 1); if (item.prefixes[i]) bits.write(item.prefixes[i], 11);
            bits.write(item.suffixes[i] != 0, 1); if (item.suffixes[i]) bits.write(item.suffixes[i], 11);
        }
        break;
    default: throw std::runtime_error("Unsupported native item quality");
    }
    if (item.flags & 0x4000000) bits.write(item.runewordId, 16);
    if (item.flags & 0x1000000) { for (const unsigned char c : item.personalizedName) bits.write(c, 7); bits.write(0, 7); }
    if (def->family == ItemFamily::Armor) value(bits, data, 31, item.defense);
    if (def->family != ItemFamily::Misc) {
        value(bits, data, 73, item.maxDurability); if (item.maxDurability) value(bits, data, 72, item.durability);
    }
    if (flag("stackable")) bits.write(item.quantity, 9);
    if (item.flags & 0x800) bits.write(item.sockets, format(data, 194).bits);
    if (identified) {
        unsigned mask = 0;
        for (size_t i = 0; i < item.setStats.size(); ++i) if (!item.setStats[i].empty()) mask |= 1u << i;
        if (item.quality == 5) bits.write(mask, 5);
        stats(bits, data, item.stats);
        for (const auto &list : item.setStats) if (!list.empty()) stats(bits, data, list);
        if (item.flags & 0x4000000) stats(bits, data, item.runewordStats);
    }
    return bits.release();
}
}
std::vector<Bytes> nativeInventoryPackets(const ClassicData &data, const PersistentCharacter &state) {
    std::vector<Bytes> result;
    std::function<void(const ItemInstance &, unsigned, uint32_t)> emit = [&](const ItemInstance &item, unsigned ownerType, uint32_t owner) {
        if (item.id.value > UINT32_MAX) throw std::runtime_error("Item ID exceeds native protocol capacity");
        auto saved = exportD2sItem(state, item, data);
        // D2S stores fixed weapon sets; GS body 4/5 always mean active hands.
        // The common client maps these through the original 0x97 weapon-set state.
        if (saved.mode == 1 && state.player.weaponSet) {
            if (saved.body == 4 || saved.body == 5) saved.body += 7;
            else if (saved.body == 11 || saved.body == 12) saved.body -= 7;
        }
        const auto body = packed(data, saved);
        if (body.size() + 13 > 255) throw std::runtime_error("Item exceeds native packet capacity");
        const auto *def = data.items.find(item.definition);
        unsigned component = unsigned(def->appearance.component);
        if (saved.mode == 1 && (saved.body == 4 || saved.body == 11)) component = 5;
        if (saved.mode == 1 && (saved.body == 5 || saved.body == 12)) component = def->equipment.isType("shld") ? 7 : 6;
        net::protocol::Writer out;
        out.u8(0x9D); out.u8(saved.mode == 1 ? 6 : saved.mode == 2 ? 14 : saved.mode == 6 ? 19 : saved.mode == 4 ? 18 : 4);
        out.u8(uint8_t(body.size() + 13)); out.u8(uint8_t(component)); out.u32(uint32_t(item.id.value));
        out.u8(uint8_t(ownerType)); out.u32(owner); out.append(body); result.push_back(out.release());
        for (const auto &child : item.socketedItems) emit(child, 4, uint32_t(item.id.value));
    };
    for (const auto &[id, item] : state.inventory.items) {
        (void)id;
        // Corpse/hireling items remain in the server save; they are not player inventory.
        const auto *location = std::get_if<ContainerLocation>(&item.location);
        if (!location || location->container == state.containers.hirelingEquipment) continue;
        const auto container = state.inventory.containers.find(location->container);
        if (container == state.inventory.containers.end() || container->second.spec.owner != state.player.id ||
            container->second.spec.kind == ContainerKind::Corpse) continue;
        emit(item, 0, uint32_t(state.player.id.value));
    }
    return result;
}
}
