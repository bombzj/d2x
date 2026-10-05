#include "remote_world.hpp"
#include "network/protocol/bits.hpp"
#include <algorithm>

namespace d2x::net {
namespace {
using namespace protocol;
OnlinePoint point(Reader &r) {
    const auto x = r.u16();
    return {x, r.u16()};
}
OnlineUnitKey key(Reader &r) {
    const auto type = r.u8();
    return {type, r.u32()};
}
OnlineUnit &unit(OnlineWorldView &w, OnlineUnitKey k) {
    if (k.type > 5)
        throw ProtocolError("Invalid game unit type");
    if (!w.units.contains(k) && w.units.size() >= 8192)
        throw ProtocolError("Remote unit limit exceeded");
    auto &u = w.units[k];
    u.key = k;
    return u;
}
void mapEvent(OnlineWorldView &w, OnlineMapEvent::Kind kind, OnlinePoint p = {}, uint8_t level = 0) {
    w.mapEvents.push_back({++w.mapEventSequence, kind, level, p});
    if (w.mapEvents.size() > 4096) w.mapEvents.pop_front();
}
void playerPosition(OnlineWorldView &w, OnlinePoint p) {
    w.playerPosition = p;
    mapEvent(w, OnlineMapEvent::Kind::PlayerPosition, p);
}
void removePlayer(OnlineWorldView &w) {
    w.playerPosition.reset();
    mapEvent(w, OnlineMapEvent::Kind::RemovePlayer);
}
void position(OnlineView &v, OnlineUnit &u, OnlinePoint p) {
    u.position = p;
    u.positionRevision = v.world.revision;
    if (u.key.type == 0 && v.load.playerUnitId == u.key.id)
        playerPosition(v.world, p);
}
std::string name(Reader &r) {
    auto bytes = r.take(16);
    const auto end = std::find(bytes.begin(), bytes.end(), uint8_t{});
    if (end == bytes.end())
        throw ProtocolError("Unterminated game player name");
    return {bytes.begin(), end};
}
void equipment(OnlineView &v, const Packet &p) {
    Reader r(p.body);
    const auto action = r.u8(), size = r.u8(), component = r.u8();
    if (size != p.body.size() + 1)
        throw ProtocolError("Invalid item packet size");
    const auto id = r.u32();
    uint8_t ownerType = 0;
    auto owner = v.load.playerUnitId;
    if (p.id == 0x9D) {
        ownerType = r.u8();
        owner = r.u32();
    }
    if (ownerType != 0 || !owner)
        return;
    // Only the visual prefix is decoded. This is not an inventory/item-stat consumer.
    // Any unequip/removal/action replaces the old appearance by item identity.
    if (auto old = v.world.equipment.find(id); old != v.world.equipment.end()) {
        if (auto oldOwner = v.world.units.find({0, old->second.owner}); oldOwner != v.world.units.end())
            ++oldOwner->second.appearanceRevision;
        v.world.equipment.erase(old);
    }
    if (p.id == 0x9C && action <= 3)
        return; // Ground items have no player owner.
    auto &u = unit(v.world, {0, *owner});
    u.equipmentObserved = true;
    ++u.appearanceRevision;
    if (action != 6 && action != 7 && action != 9 && action != 21 && action != 23)
        return;
    BitReader bits(r.take(r.remaining()));
    OnlineEquippedItem item;
    item.id = id;
    item.owner = *owner;
    item.component = component;
    item.flags = bits.read(32);
    bits.read(10); // Native client item format.
    const auto location = bits.read(3);
    if (location == 3 || location == 5) {
        bits.read(16);
        bits.read(16);
    } else {
        item.bodyLocation = uint8_t(bits.read(4));
        bits.read(4);
        bits.read(4);
        bits.read(3);
    }
    if (item.flags & 0x10000)
        return; // Ear layout has no ordinary base code.
    for (int i = 0; i < 4; ++i)
        item.code += char(bits.read(8));
    while (!item.code.empty() && item.code.back() == ' ')
        item.code.pop_back();
    if (location != 1 || item.bodyLocation == 0 || item.code.empty())
        return;
    if (!(item.flags & (0x200000 | 0x2000000))) {
        bits.read(3); // Filled sockets.
        bits.read(7); // Item level; no server seed in client serialization.
        item.quality = uint8_t(bits.read(4));
        if (bits.read(1))
            bits.read(3);
        item.autoAffix = bits.read(1) != 0;
    }
    // One server body slot has one item; replace it atomically on equip/swap.
    std::erase_if(v.world.equipment, [&](const auto &e) {
        return e.second.owner == item.owner && e.second.bodyLocation == item.bodyLocation;
    });
    if (v.world.equipment.size() >= 2048)
        throw ProtocolError("Remote equipment limit exceeded");
    v.world.equipment[id] = std::move(item);
}
} // namespace
void apply_world_packet(OnlineView &v, const protocol::Packet &p) {
    auto &w = v.world;
    ++v.revision;
    ++w.revision;
    Reader r(p.body);
    switch (p.id) {
    case 0x07:
    case 0x08: {
        const auto anchor = point(r);
        const auto area = r.u8();
        r.finish();
        const auto k = std::tuple{area, anchor.x, anchor.y};
        if (p.id == 0x08) {
            w.rooms.erase(k);
            w.roomAssignmentRevisions.erase(k);
        } else {
            if (!w.rooms.contains(k) && w.rooms.size() >= 4096)
                throw ProtocolError("Remote room limit exceeded");
            w.rooms[k] = anchor;
            w.roomAssignmentRevisions.try_emplace(k, w.revision);
        }
        mapEvent(w, p.id == 0x07 ? OnlineMapEvent::Kind::RevealRoom : OnlineMapEvent::Kind::HideRoom,
            anchor, area);
        break;
    }
    case 0x09: {
        // SCmd/SUnitMsg: native warp assignment, byte-sized LvlWarp identity.
        auto &u = unit(w, key(r));
        u.classId = r.u8();
        position(v, u, point(r));
        r.finish();
        break;
    }
    case 0x0A: {
        const auto k = key(r);
        r.finish();
        w.units.erase(k);
        if (k.type == 0) {
            std::erase_if(w.equipment, [&](const auto &e) { return e.second.owner == k.id; });
            if (v.load.playerUnitId == k.id)
                removePlayer(w);
        }
        break;
    }
    case 0x0B: {
        const auto k = key(r);
        r.finish();
        if (k.type == 0) {
            auto &u = unit(w, k);
            if (v.load.playerUnitId == k.id && u.position)
                playerPosition(w, *u.position);
            else if (w.playerPosition)
                position(v, u, *w.playerPosition);
        }
        break;
    }
    case 0x0D: {
        auto &u = unit(w, key(r));
        u.mode = r.u8();
        position(v, u, point(r));
        r.u8();
        u.lifePercent = r.u8();
        r.finish();
        u.destination.reset();
        break;
    }
    case 0x0E: {
        auto &u = unit(w, key(r));
        r.u8();
        r.u8();
        const auto mode = r.u32();
        if (mode <= 255)
            u.mode = uint8_t(mode);
        r.finish();
        break;
    }
    case 0x0F: {
        auto &u = unit(w, key(r));
        u.mode = r.u8();
        u.destination = point(r);
        r.u8();
        position(v, u, point(r));
        r.finish();
        break;
    }
    case 0x10: {
        auto &u = unit(w, key(r));
        u.mode = r.u8();
        r.u8();
        r.u32();
        position(v, u, point(r));
        r.finish();
        u.destination.reset();
        break;
    }
    case 0x15: {
        auto &u = unit(w, key(r));
        position(v, u, point(r));
        r.u8();
        r.finish();
        u.destination.reset();
        break;
    }
    case 0x16: {
        const auto length = r.u16();
        const auto count = r.u8();
        if (length != p.body.size() + 1 || r.remaining() != size_t(count) * 9)
            throw ProtocolError("Invalid unit update batch");
        for (int i = 0; i < count; ++i) {
            auto &u = unit(w, key(r));
            position(v, u, point(r));
        }
        break;
    }
    case 0x1D:
    case 0x1E:
    case 0x1F: {
        const auto attribute = r.u8();
        w.playerAttributes[attribute] = p.id == 0x1D ? r.u8() : p.id == 0x1E ? r.u16() : r.u32();
        r.finish();
        break;
    }
    case 0x51: {
        auto &u = unit(w, key(r));
        u.classId = r.u16();
        position(v, u, point(r));
        u.mode = r.u8();
        r.u8();
        r.finish();
        break;
    }
    case 0x59: {
        auto &u = unit(w, {0, r.u32()});
        u.classId = r.u8();
        u.name = name(r);
        if (!u.mode)
            u.mode = 7; // Native player assignment starts neutral.
        position(v, u, point(r));
        r.finish();
        break;
    }
    case 0x5C: {
        const auto id = r.u32();
        r.finish();
        w.units.erase({0, id});
        std::erase_if(w.equipment, [&](const auto &e) { return e.second.owner == id; });
        if (v.load.playerUnitId == id)
            removePlayer(w);
        break;
    }
    case 0x67:
    case 0x68: {
        auto &u = unit(w, {1, r.u32()});
        r.u8();
        u.mode.reset();
        // These coordinates are a path target, not a verified current position.
        u.destination = point(r);
        break;
    }
    case 0x6B:
    case 0x6C: {
        auto &u = unit(w, {1, r.u32()});
        r.u8();
        r.take(6);
        position(v, u, point(r));
        r.finish();
        break;
    }
    case 0x6D: {
        auto &u = unit(w, {1, r.u32()});
        position(v, u, point(r));
        u.lifePercent = r.u8();
        r.finish();
        u.mode = 1;
        u.destination.reset();
        break;
    }
    case 0x18:
    case 0x95:
    case 0x96: {
        BitReader bits(p.body);
        if (p.id != 0x96) {
            w.life = uint16_t(bits.read(15));
            w.mana = uint16_t(bits.read(15));
        }
        w.stamina = uint16_t(bits.read(15));
        if (p.id == 0x18) {
            bits.read(7);
            bits.read(7);
        }
        const auto x = uint16_t(bits.read(16)), y = uint16_t(bits.read(16));
        // The last bytes are signed offsets to the first path point, not fractions.
        bits.read(8);
        bits.read(8);
        if (v.load.playerUnitId)
            position(v, unit(w, {0, *v.load.playerUnitId}), {x, y});
        else
            playerPosition(w, {x, y});
        break;
    }
    case 0x9C:
    case 0x9D:
        equipment(v, p);
        break;
    case 0xAC: {
        auto &u = unit(w, {1, r.u32()});
        u.classId = r.u16();
        position(v, u, point(r));
        u.lifePercent = r.u8();
        if (r.u8() != p.body.size() + 1)
            throw ProtocolError("Invalid NPC assignment size");
        const auto tail = r.take(r.remaining());
        u.appearanceBits.assign(tail.begin(), tail.end());
        if (!tail.empty()) {
            BitReader bits(tail);
            u.mode = uint8_t(bits.read(4));
        }
        ++u.appearanceRevision;
        break;
    }
    default:
        ++w.ignoredPackets;
        return;
    }
}
} // namespace d2x::net
