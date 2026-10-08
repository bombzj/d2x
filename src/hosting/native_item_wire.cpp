#include "native_item_wire.hpp"
#include "persistence/d2s_inventory.hpp"
#include "network/protocol/wire.hpp"
#include "network/protocol/bits.hpp"
#include "server/runtime/events.hpp"
#include "protocol/message_catalog.hpp"
#include <algorithm>
#include <map>

namespace d2x {
namespace {
using net::protocol::BitWriter;
// D2MOO D2Constants / SCmd original item action identities, not C2S opcodes.
enum class ItemAction : uint8_t {
    Put = 4, Take = 5, Equip = 6, IndirectEquip = 7, Unequip = 8, SwapEquipment = 9, Quantity = 10, Properties = 21,
    SwapStored = 13, PutBelt = 14, TakeBelt = 15, SwapBelt = 16, Cursor = 18, Socket = 19, WeaponSwitch = 23
};
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
void emitItem(std::vector<Bytes> &result, const ClassicData &data, const PersistentCharacter &state,
              const ItemInstance &item, ItemAction action, bool ownedPacket, unsigned ownerType, uint32_t owner) {
    if (item.id.value > UINT32_MAX) throw std::runtime_error("Item ID exceeds native protocol capacity");
    auto saved = exportD2sItem(state, item, data);
    saved.flags = (saved.flags & ~0x4000u) | (item.nativeFlags & 0x4000u);
    // D2S stores fixed weapon sets; GS body 4/5 always mean active hands.
    // The common client maps these through the original 0x97 weapon-set state.
    if (saved.mode == 1 && state.player.weaponSet) {
        if (saved.body == 4 || saved.body == 5) saved.body += 7;
        else if (saved.body == 11 || saved.body == 12) saved.body -= 7;
    }
    if (action == ItemAction::WeaponSwitch) {
        // Native transient SWITCHIN/OUT flags; never write these to D2S.
        saved.flags &= ~uint32_t(0xC0);
        saved.flags |= (saved.body == 4 || saved.body == 5) ? 0x40 : 0x80;
    }
    const auto body = packed(data, saved);
    const size_t prefix = ownedPacket ? 13 : 8;
    if (body.size() + prefix > 255) throw std::runtime_error("Item exceeds native packet capacity");
    const auto *def = data.items.find(item.definition);
    unsigned component = unsigned(def->appearance.component);
    if (saved.mode == 1 && (saved.body == 4 || saved.body == 11)) component = 5;
    if (saved.mode == 1 && (saved.body == 5 || saved.body == 12)) component = def->equipment.isType("shld") ? 7 : 6;
    net::protocol::Writer out;
    out.u8(ownedPacket ? 0x9D : 0x9C); out.u8(uint8_t(action));
    out.u8(uint8_t(body.size() + prefix)); out.u8(uint8_t(component)); out.u32(uint32_t(item.id.value));
    if (ownedPacket) { out.u8(uint8_t(ownerType)); out.u32(owner); }
    out.append(body); result.push_back(out.release());
    for (const auto &child : item.socketedItems)
        emitItem(result, data, state, child, ItemAction::Socket, true, 4, uint32_t(item.id.value));
}
bool equipment(ContainerKind kind) { return kind == ContainerKind::Equipment || kind == ContainerKind::BeltEquipment; }
ContainerKind kind(const PersistentCharacter &state, const std::optional<ItemLocation> &location) {
    const auto *position = location ? std::get_if<ContainerLocation>(&*location) : nullptr;
    if (!position) throw std::logic_error("Inventory slice contains a non-container change");
    return state.inventory.containers.at(position->container).spec.kind;
}
}
std::vector<Bytes> nativeInventoryPackets(const ClassicData &data, const PersistentCharacter &state) {
    std::vector<Bytes> result;
    for (const auto &[id, item] : state.inventory.items) {
        (void)id;
        // Corpse/hireling items remain in the server save; they are not player inventory.
        const auto *location = std::get_if<ContainerLocation>(&item.location);
        if (!location || location->container == state.containers.hirelingEquipment) continue;
        const auto container = state.inventory.containers.find(location->container);
        if (container == state.inventory.containers.end() || container->second.spec.owner != state.player.id ||
            container->second.spec.kind == ContainerKind::Corpse) continue;
        const auto kind = container->second.spec.kind;
        emitItem(result, data, state, item, equipment(kind) ? ItemAction::Equip : kind == ContainerKind::Belt ? ItemAction::PutBelt :
            kind == ContainerKind::Cursor ? ItemAction::Cursor : ItemAction::Put,
            true, 0, uint32_t(state.player.id.value));
    }
    return result;
}
std::vector<Bytes> nativePublicEquipment(const ClassicData &data, PersistentCharacter projection) {
    for (auto &[id, item] : projection.inventory.items) {
        (void)id;
        const auto conceal = [&](auto &&self, ItemInstance &value) -> void {
            value.identified = false; value.nativeFlags &= ~uint32_t(0x10);
            for (auto &child : value.socketedItems) self(self, child);
        };
        conceal(conceal, item);
    }
    return nativeInventoryPackets(data, projection);
}
std::vector<Bytes> nativeInventoryDelta(const ClassicData &data, const server::InventoryFact &fact) {
    const auto &state = fact.projection;
    if (state.player.id.value > UINT32_MAX) throw std::runtime_error("Inventory owner exceeds native GUID capacity");
    std::vector<Bytes> result;
    for (const auto &change : fact.changes) {
        if (change.item.value > UINT32_MAX) throw std::runtime_error("Inventory delta GUID overflow");
        if (change.kind == ItemChangeKind::Removed) {
            result.push_back(hosting::encodeServerPacket(hosting::ServerMessage::RemoveUnit,
                [&](auto &out) { out.u8(4); out.u32(uint32_t(change.item.value)); }));
            continue;
        }
        if (change.kind == ItemChangeKind::QuantityChanged || change.kind == ItemChangeKind::PropertiesChanged) {
            const auto action = change.kind == ItemChangeKind::QuantityChanged ? ItemAction::Quantity : ItemAction::Properties;
            emitItem(result, data, state, state.inventory.items.at(change.item), action,
                action == ItemAction::Properties, 0, uint32_t(state.player.id.value));
            continue;
        }
        if (change.kind != ItemChangeKind::Moved) throw std::logic_error("Unsupported inventory delta");
        const auto from = kind(state, change.before), to = kind(state, change.after);
        ItemAction action;
        bool owned;
        const auto replaced = std::find_if(fact.changes.begin(), fact.changes.end(), [&](const auto &other) {
            return other.item != change.item && other.before == change.after;
        }) != fact.changes.end();
        if (to == ContainerKind::Cursor) {
            action = equipment(from) ? ItemAction::Unequip : from == ContainerKind::Belt ? ItemAction::TakeBelt : ItemAction::Take;
            owned = equipment(from) || from == ContainerKind::Backpack;
        }
        else if (equipment(to)) {
            const bool indirect = !replaced && std::any_of(fact.changes.begin(), fact.changes.end(), [&](const auto &other) {
                return other.kind == ItemChangeKind::Moved && other.item != change.item && equipment(kind(state, other.before)) && kind(state, other.after) == ContainerKind::Cursor;
            });
            action = replaced ? ItemAction::SwapEquipment : indirect ? ItemAction::IndirectEquip : ItemAction::Equip;
            owned = true;
        }
        else if (to == ContainerKind::Belt) { action = replaced ? ItemAction::SwapBelt : ItemAction::PutBelt; owned = false; }
        else if (to == ContainerKind::Backpack) { action = replaced ? ItemAction::SwapStored : ItemAction::Put; owned = false; }
        else throw std::logic_error("Unsupported inventory destination");
        emitItem(result, data, state, state.inventory.items.at(change.item), action, owned, 0, uint32_t(state.player.id.value));
    }
    if (fact.switchedWeapons) {
        for (const auto &[id, item] : state.inventory.items) {
            (void)id;
            const auto *at = std::get_if<ContainerLocation>(&item.location);
            if (!at || at->container != state.containers.equipment) continue;
            const auto slot = EquipmentSlot(at->cell.x);
            if (slot == EquipmentSlot::RightHand || slot == EquipmentSlot::LeftHand ||
                slot == EquipmentSlot::AlternateRightHand || slot == EquipmentSlot::AlternateLeftHand)
                emitItem(result, data, state, item, ItemAction::WeaponSwitch, true, 0, uint32_t(state.player.id.value));
        }
        result.push_back(hosting::encodeServerPacket(hosting::ServerMessage::WeaponSet, [](auto &) {}));
        for (size_t side = 0; side < 2; ++side)
            result.push_back(hosting::encodeServerPacket(hosting::ServerMessage::SelectedSkill, [&](auto &out) {
                out.u8(0); out.u32(uint32_t(state.player.id.value)); out.u8(side == 0);
                out.u16(uint16_t(std::max(0, state.player.selectedSkills[state.player.weaponSet * 2 + side]))); out.u32(UINT32_MAX);
            }));
    }
    return result;
}
}
