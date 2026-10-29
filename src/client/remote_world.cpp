#include "remote_world.hpp"
#include "remote_social.hpp"
#include "network/protocol/bits.hpp"
#include <algorithm>
#include <bit>
#include <chrono>

namespace d2x::net {
namespace {
using namespace protocol;
uint64_t receivedMilliseconds() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
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
void combatEvent(OnlineWorldView &w, OnlineCombatEvent event) {
    if (event.kind == OnlineCombatEvent::Kind::Skill &&
        (event.packet == 0x4C || event.packet == 0x4D)) {
        // Retail 0x99/0x9A invoke an item-effect skill without changing mode,
        // target or animation epoch. In particular PD2 Skeleton Splash has
        // monanim=xx; replacing the attack with it hides the actor until NU.
        // Keep those notifications in combatEvents for their client effects.
        // ObjMode also sends 0x4D for shrine operation: the apparent skill
        // is the activating player's GUID and level is the shrine type.
        // Only players and monsters carry skill actions. Objects retain
        // their authoritative 0x0E mode and must not emit a spell effect.
        if (event.source.type > 1) return;
        auto &actor = unit(w, event.source);
        actor.actionSkill = event.skill; actor.actionSkillLevel = event.level;
        if (actor.key.type == 1 && event.skill && event.level)
            actor.skills.insert_or_assign(*event.skill, *event.level);
        actor.actionRevision = w.revision; actor.actionReceivedMilliseconds = receivedMilliseconds();
        actor.destination = event.point; actor.destinationUnit = event.target;
        actor.direction.reset(); actor.nativeMode = false;
        // Skill packets replace the player's action byte. Monsters resolve monanim in MPQ.
        if (actor.key.type == 0) actor.mode = 21;
        else actor.mode.reset();
    }
    event.receivedMilliseconds = receivedMilliseconds();
    event.sequence = ++w.combatSequence;
    w.combatEvents.push_back(std::move(event));
    if (w.combatEvents.size() > 256) w.combatEvents.pop_front();
}
void monsterAction(OnlineWorldView &w, OnlineUnit &u, uint8_t action) {
    u.actionSkill.reset(); u.actionSkillLevel.reset(); u.direction.reset();
    u.wireAction = action; u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds();
    // MonsterMsg's native wire actions are not MONMODE indices.
    switch (action) {
    case 0: case 1: u.mode = 2; break;
    case 23: case 24: u.mode = 15; break;
    case 7: u.mode = 1; break;
    case 8: u.mode = 0; u.lifePercent = 0; u.lifeCarriesTriggerFlag = false; break;
    case 9: u.mode = 12; u.lifePercent = 0; u.lifeCarriesTriggerFlag = false; break;
    case 6: u.mode = 3; break;
    case 10: case 11: u.mode = 4; break;
    case 16: case 17: u.mode = 5; break;
    case 18: u.mode = 6; break;
    case 4: case 5: u.mode = 7; break;
    case 12: case 13: u.mode = 8; break;
    case 14: case 15: u.mode = 9; break;
    case 26: case 27: u.mode = 10; break;
    case 28: case 29: u.mode = 11; break;
    case 20: u.mode = 13; break;
    // MonsterMsg returns before this action table for every SEQUENCE, sending
    // a skill packet when it has a used skill. Thus raw 12/13 uniquely mean S1.
    default: u.mode.reset(); break;
    }
}
void stateMessage(OnlineUnit &u, OnlineStateMessage message) {
    message.sequence = ++u.stateSequence;
    if (message.kind == OnlineStateMessage::Kind::Snapshot) u.stateMessages.clear();
    u.stateMessages.push_back(std::move(message));
    if (u.stateMessages.size() > 64) u.stateMessages.pop_front();
}
void mapEvent(OnlineWorldView &w, OnlineMapEvent::Kind kind, OnlinePoint p = {}, uint8_t level = 0) {
    w.mapEvents.push_back({++w.mapEventSequence, kind, level, p});
    if (w.mapEvents.size() > 4096) w.mapEvents.pop_front();
}
void playerPosition(OnlineWorldView &w, OnlinePoint p) {
    w.playerPosition = p;
    mapEvent(w, OnlineMapEvent::Kind::PlayerPosition, p);
}
void playerMode(OnlineView &v, const OnlineUnit &u) {
    if (u.key.type != 0 || v.load.playerUnitId != u.key.id || !u.mode) return;
    auto &w = v.world;
    const bool dying = u.nativeMode ? u.mode == 0 : u.mode == 8;
    const bool dead = u.nativeMode ? u.mode == 17 : u.mode == 9;
    if (dying || dead) {
        if (w.playerTrade.active()) { ++w.interactionGeneration; w.playerTrade = {}; }
        if (w.npcRequested || w.npcConversation || w.waypointSource || w.waypointRequested) ++w.interactionGeneration;
        if (!onlinePlayerDead(w)) w.respawnRequest.reset();
        const auto phase = dead || w.deathPhase == OnlineDeathPhase::Dead ? OnlineDeathPhase::Dead : OnlineDeathPhase::Dying;
        if (w.deathPhase != phase) { w.deathPhase = phase; w.deathRevision = w.revision; }
        w.movementRequest.reset(); w.npcRequested.reset(); w.npcPendingConversation.reset(); w.npcConversation.reset(); w.waypointSource.reset();
        w.waypointRequested.reset();
        w.waypointActivation.reset();
        w.staffSource.reset();
    } else if (w.deathPhase == OnlineDeathPhase::Unknown) {
        w.deathPhase = OnlineDeathPhase::Alive; w.deathRevision = w.revision;
    } else if (onlinePlayerDead(w) && w.respawnRequest && w.respawnRequest->sent && u.position &&
               (u.nativeMode ? (u.mode == 1 || u.mode == 5) : u.mode == 7)) {
        w.deathPhase = OnlineDeathPhase::Alive; w.deathRevision = w.revision;
        w.respawnRequest->state = OnlineRespawnRequest::State::Confirmed;
        // Discard pre-resurrection resource samples, use original stat updates until
        // the next 0x95. Never supply locally guessed max resources.
        w.life.reset(); w.mana.reset(); w.stamina.reset();
    }
}
void resurrectionUpdates(OnlineView &v) {
    auto &w = v.world;
    if (!onlinePlayerDead(w) || !w.respawnRequest || !w.respawnRequest->sent ||
        w.respawnRequest->restoredResources != 7 || !w.respawnRequest->repositioned ||
        !v.load.playerUnitId) return;
    const auto found = w.units.find({0, *v.load.playerUnitId});
    if (found == w.units.end() || !found->second.position) return;
    // Rcv0x41 restores all three absolute stats and LEVEL_WarpUnit sends
    // 0x15. The owning client need not receive a neutral mode packet.
    auto &u = found->second;
    u.mode = 7; u.nativeMode = false; u.actionSkill.reset(); u.actionSkillLevel.reset();
    u.lifePercent.reset(); u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds();
    playerMode(v, u);
}
void removePlayer(OnlineWorldView &w) {
    ++w.interactionGeneration;
    w.playerTrade = {};
    w.waypointSource.reset();
    w.waypointRequested.reset();
    w.waypointActivation.reset();
    w.npcRequested.reset(); w.npcPendingConversation.reset(); w.npcConversation.reset(); w.movementRequest.reset();
    w.staffSource.reset();
    w.townPortalPending = false;
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
    if (ownerType > 1 || !owner || (ownerType==1 && action==11))
        return;
    // Only the visual prefix is decoded. This is not an inventory/item-stat consumer.
    // Any unequip/removal/action replaces the old appearance by item identity.
    if (auto old = v.world.equipment.find(id); old != v.world.equipment.end()) {
        if (auto oldOwner = v.world.units.find({old->second.ownerType, old->second.owner}); oldOwner != v.world.units.end())
            ++oldOwner->second.appearanceRevision;
        v.world.equipment.erase(old);
    }
    if (p.id == 0x9C && action <= 3)
        return; // Ground items have no player owner.
    auto &u = unit(v.world, {ownerType, *owner});
    u.equipmentObserved = true;
    ++u.appearanceRevision;
    if (action == 5 || action == 8 || action == 12 || action == 15 || action == 17)
        return;
    BitReader bits(r.take(r.remaining()));
    OnlineEquippedItem item;
    item.id = id;
    item.owner = *owner;item.ownerType=ownerType;
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
        return e.second.ownerType==item.ownerType && e.second.owner == item.owner && e.second.bodyLocation == item.bodyLocation;
    });
    if (v.world.equipment.size() >= 2048)
        throw ProtocolError("Remote equipment limit exceeded");
    v.world.equipment[id] = std::move(item);
}
void removeItem(OnlineWorldView &world, uint32_t id);
}
void invalidateCubeHostPd2(OnlineView &view) {
    auto &world = view.world;
    const auto accessible = [&](std::optional<uint32_t> source) {
        const auto found = source ? world.items.find(*source) : world.items.end();
        if (found == world.items.end()) return false;
        const auto &item = found->second;
        return onlineItemOwnedBy(item,view.load.playerUnitId) && item.positionKnown &&
            item.mode == 0 && !(item.flags & (0x40000000u | 0x2000000u)) &&
            (item.page == 1 || (item.page == 5 && world.cnStashConfirmed && world.stashSynchronized &&
                !world.requestedStashPage && item.stashPage == world.stashPage && item.sharedStash == world.sharedStash));
    };
    if ((world.storage.kind == OnlineStorageKind::Cube && !accessible(world.storage.source)) ||
        (world.storage.requested == OnlineStorageKind::Cube && !accessible(world.storage.requestedSource))) {
        ++world.interactionGeneration;
        world.storage = {};
        if (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending)
            world.itemRequest->state = OnlineItemRequest::State::Interrupted;
    }
}
namespace {
void itemPacket(OnlineView &v, const Packet &p, bool pd2) {
    if (v.world.playerTrade.phase == OnlinePlayerTrade::Phase::Open)
        v.world.playerTrade.revision = v.world.revision;
    Reader r(p.body);
    OnlineItem item;
    item.action = r.u8();
    if (r.u8() != p.body.size() + 1) throw ProtocolError("Invalid native item packet size");
    item.component = r.u8(); item.id = r.u32();
    if (item.action == 12) { // Explicit removal from a vendor's shelf.
        v.world.items.erase(item.id); ++v.world.itemRevision; return;
    }
    if (p.id == 0x9D) { item.ownerType = r.u8(); item.owner = r.u32(); }
    const auto packed = r.take(r.remaining()); item.packed.assign(packed.begin(), packed.end());
    BitReader bits(packed);
    item.flags = bits.read(32); bits.read(10); item.mode = uint8_t(bits.read(3));
    if (p.id == 0x9D && item.action == 8 && item.ownerType == 0 && item.owner != v.load.playerUnitId) {
        const auto previous = v.world.items.find(item.id);
        if (previous != v.world.items.end() && previous->second.mode == 1 &&
            previous->second.ownerType == item.ownerType && previous->second.owner == item.owner)
            removeItem(v.world, item.id);
        return;
    }
    // A successful sale receipt can precede the seller's removal packet.
    // Its ONCURSOR mode describes equipment removal, not a new owned cursor.
    if ((item.action == 5 || item.action == 8 || item.action == 15 || item.action == 17) &&
        v.world.tradeResult && v.world.tradeResult->result == 1 &&
        v.world.tradeResult->item == item.id && !v.world.items.contains(item.id)) {
        removeItem(v.world, item.id);
        return;
    }
    // Taking an item can combine removal with its new cursor mode in the same packet.
    if ((item.action == 5 || item.action == 15) && item.mode != 4) {
        removeItem(v.world, item.id);
        return;
    }
    if (item.mode == 3 || item.mode == 5) {
        item.groundX = uint16_t(bits.read(16)); item.groundY = uint16_t(bits.read(16));
    } else {
        item.body = uint8_t(bits.read(4)); item.x = uint8_t(bits.read(4));
        item.y = uint8_t(bits.read(4)); item.page = uint8_t(bits.read(3));
    }
    // PlrTrade sub_6FC931D0 / ItemMode sub_6FC446B0 remove the peer's
    // INVPAGE_EQUIP clone with ONCURSOR mode, but never assign its cursor.
    if (item.action == 5 && item.mode == 4 && item.page == 2) {
        removeItem(v.world,item.id);
        return;
    }
    if (item.flags & 0x10000) item.code = "ear"; // Original ear payload is decoded in the MPQ consumer.
    else {
        for (int i = 0; i < 4; ++i) item.code += char(bits.read(8));
        while (!item.code.empty() && (item.code.back() == ' ' || !item.code.back())) item.code.pop_back();
    }
    if (p.id == 0x9C && item.mode != 3 && item.mode != 5 && item.action != 0 && item.action != 2 && item.action != 3 &&
        item.action != 11 && item.action != 12) {
        item.ownerType = 0; item.owner = v.load.playerUnitId;
    }
    // 0x9C inventory packets may precede 0x0B; their player is this connection.
    if (!item.owner && item.ownerType == 0 && v.load.playerUnitId) item.owner = v.load.playerUnitId;
    if (p.id == 0x9C && item.action == 11 && v.world.shopRequested &&
        v.world.npcRequested == v.world.shopRequested) {
        item.ownerType = 1; item.owner = v.world.shopRequested;
        v.world.shopSource = v.world.shopRequested;
    }
    item.revision = ++v.world.itemRevision;
    if (pd2 && item.mode == 0 && item.page == 5) {
        item.stashPage = v.world.requestedStashPage.value_or(v.world.stashPage);
        item.sharedStash = v.world.requestedStashPage ? v.world.requestedSharedStash : v.world.sharedStash;
        // Before the control reply there is no authoritative page identity in
        // an ordinary item packet. Require a placement refresh after selection.
        if (v.world.requestedStashPage || !v.world.cnStashConfirmed || !v.world.stashSynchronized)
            item.positionKnown = false;
    }
    // The compact network position has four-bit coordinates. Cached stash
    // placements need the full-byte 0x2F update before they can be projected.
    if (pd2 && item.mode == 0 && item.page == 6) item.positionKnown = false;
    if (item.mode == 6 && item.ownerType == 4) {
        const auto previous = v.world.items.find(item.id);
        item.socketAssignmentRevision = previous != v.world.items.end() && previous->second.mode == 6 &&
            previous->second.ownerType == 4 && previous->second.owner == item.owner && previous->second.socketAssignmentRevision
            ? previous->second.socketAssignmentRevision : item.revision;
    }
    if (item.mode == 3 || item.mode == 5) {
        const auto previous = v.world.items.find(item.id);
        const bool alreadyDropping = previous != v.world.items.end() && previous->second.mode == 5;
        // ITEMACTION_DROPTOGROUND is a flip event even though the native
        // server item mode is ONGROUND. ADDTOGROUND/ONGROUND alone are snapshots.
        if (item.action == 2 || (item.mode == 5 && !alreadyDropping)) {
            item.groundAnimationRevision = item.revision;
            item.groundAnimationReceivedMilliseconds = receivedMilliseconds();
        } else if (previous != v.world.items.end() && (previous->second.mode == 3 || previous->second.mode == 5)) {
            item.groundAnimationRevision = previous->second.groundAnimationRevision;
            item.groundAnimationReceivedMilliseconds = previous->second.groundAnimationReceivedMilliseconds;
        }
    }
    if (!v.world.items.contains(item.id) && v.world.items.size() >= 8192)
        throw ProtocolError("Remote item limit exceeded");
    v.world.items[item.id] = std::move(item);
    if (pd2) invalidateCubeHostPd2(v);
}
void removeItem(OnlineWorldView &world, uint32_t id) {
    std::vector<uint32_t> children;
    for (const auto &[child, item] : world.items)
        if (item.ownerType == 4 && item.owner == id) children.push_back(child);
    if (world.itemTargetingSource == id) {world.itemTargetingSource.reset();++world.itemTargetingRevision;}
    if ((world.storage.kind == OnlineStorageKind::Cube && world.storage.source == id) ||
        (world.storage.requested == OnlineStorageKind::Cube && world.storage.requestedSource == id)) {
        ++world.interactionGeneration;
        world.storage = {};
    }
    if (const auto found = world.equipment.find(id); found != world.equipment.end())
        if (const auto owner = world.units.find({found->second.ownerType, found->second.owner}); owner != world.units.end())
            ++owner->second.appearanceRevision;
    world.items.erase(id); world.equipment.erase(id); world.units.erase({4,id});
    for (const auto child : children) if (child != id) removeItem(world, child);
    ++world.itemRevision;
}
void removeRemotePlayerItems(OnlineWorldView &world, uint32_t owner,uint8_t ownerType=0) {
    std::vector<uint32_t> items;
    for (const auto &[id, item] : world.items)
        if (item.ownerType == ownerType && item.owner == owner) items.push_back(id);
    for (const auto &[id, item] : world.equipment)
        if (item.ownerType == ownerType && item.owner == owner &&
            std::find(items.begin(),items.end(),id)==items.end()) items.push_back(id);
    for (const auto id : items) removeItem(world, id);
    std::erase_if(world.equipment, [&](const auto &entry) { return entry.second.ownerType==ownerType && entry.second.owner == owner; });
}
bool applyStoragePacketPd2(OnlineView &v, const Packet &p, const OnlineStashLayout &layout) {
    auto &w = v.world;
    if (p.id == 0x9C && !p.body.empty() && p.body[0] >= 0x19) {
        Reader external(p.body);
        const auto action = external.u8(), length = external.u8();
        if (length != p.body.size() + 1)
            throw ProtocolError("Invalid PD2 external inventory length");
        if (action != 0x19 && action != 0x1A) {
            // A framed extension with an unknown action must not be decoded
            // as a normal item or interrupt unrelated gameplay.
            ++w.ignoredPackets; ++v.gameProtocol.unconsumed[p.id]; return true;
        }
        external.u8();
        const auto id = external.u32();
        auto &storage = w.extendedStorage;
        if (action == 0x19) {
            const auto payload = external.take(external.remaining());
            if (payload.empty() || payload.size() > 244 || (!storage.guildItems.contains(id) && storage.guildItems.size() >= 8192))
                throw ProtocolError("Invalid PD2 external inventory payload");
            storage.guildItems[id] = Bytes(payload.begin(), payload.end());
        } else storage.guildItems.erase(id);
        ++storage.revision;
        ++v.gameProtocol.unconsumed[p.id]; // Isolated guild inventory has no writable player projection.
        return true;
    }
    Reader r(p.body);
    switch (p.id) {
    case 0x36: {
        const auto tag = r.u32(); const auto offset = r.u16(), count = r.u16(), total = r.u16();
        const auto bytes = r.take(400); r.finish();
        auto &storage = w.extendedStorage;
        if (count && total) {
            if (count > 400 || total > 6400 || offset > total || count > total - offset)
                throw ProtocolError("Invalid PD2 guild showcase fragment bounds");
            if (!offset) { storage.fragment.clear(); storage.fragmentTag = tag; storage.fragmentTotal = total; }
            if (total != storage.fragmentTotal || offset != storage.fragment.size())
                throw ProtocolError("PD2 guild showcase fragment is not contiguous");
            storage.fragment.insert(storage.fragment.end(), bytes.begin(), bytes.begin() + count);
            if (storage.fragment.size() == total) {
                storage.showcase = std::move(storage.fragment); storage.fragment = {}; storage.fragmentTotal = 0;
                ++storage.revision;
            }
        }
        ++v.gameProtocol.unconsumed[p.id]; // Raw showcase metadata grants no inventory ownership.
        break;
    }
    case 0x2F: {
        // Core RVA 0x38F790: the +10==0 branch writes coordinates of an
        // existing item. Ownership/page must already be established by 9C/9D.
        const auto id = r.u32();
        const auto page = r.u8();
        const auto destinationPage = r.u8(); // Only consumed by the native detach-to-page-6 branch.
        const auto x = r.u8(), y = r.u8();
        const auto rebuild = r.u8(); // Clear/rebuild target grid occupancy.
        const auto detach = r.u8();
        r.finish();
        (void)destinationPage; // Detached/special placements remain unavailable below.
        if (rebuild && (page == 0 || page == 3 || page == 4 || page == 5))
            for (auto &[otherId, other] : w.items)
                if (onlineItemOwnedBy(other, v.load.playerUnitId) && other.mode == 0 &&
                    (page == 5 ? (other.page == 6 || (!w.cnStashConfirmed && !w.requestedStashPage &&
                        other.page == 5 && other.stashPage != w.stashPage)) :
                        other.page == page + 1 && (page != 4 ||
                            (other.stashPage == w.requestedStashPage.value_or(w.stashPage) &&
                             other.sharedStash == (w.requestedStashPage ? w.requestedSharedStash : w.sharedStash))))) {
                    other.positionKnown = false; other.revision = ++w.itemRevision;
                }
        if (auto item = w.items.find(id); item != w.items.end() && onlineItemOwnedBy(item->second, v.load.playerUnitId)) {
            auto &value = item->second;
            const bool personalStash = (page == 4 || page == 5) && (value.page == 5 || value.page == 6);
            if (detach || (page != 0 && page != 3 && !personalStash) ||
                value.mode != 0 || (!personalStash && value.page != page + 1)) {
                item->second.positionKnown = false; item->second.revision = ++w.itemRevision;
                ++v.gameProtocol.unconsumed[p.id];
                break;
            }
            item->second.x = x; item->second.y = y;
            item->second.positionKnown = true;
            if (page == 4) {
                value.page = 5;
                value.stashPage = w.requestedStashPage.value_or(w.stashPage);
                value.sharedStash = w.requestedStashPage ? w.requestedSharedStash : w.sharedStash;
                if (w.requestedStashPage || !w.cnStashConfirmed || !w.stashSynchronized)
                    value.positionKnown = false;
            } else if (page == 5) {
                // Core 37E480/37E530: normal pages occupy rectangles in
                // LocalStashPagesSave; the final row/column is reserved.
                if (w.cnStashConfirmed || w.requestedStashPage || !layout.valid() || x >= layout.cacheColumns || y >= layout.cacheRows) {
                    value.positionKnown = false;
                } else {
                    const unsigned pagesPerColumn = layout.cacheRows / layout.rows - 1;
                    const unsigned column = x / layout.columns, row = y / layout.rows;
                    const unsigned index = column * pagesPerColumn + row;
                    if (row >= pagesPerColumn || column >= layout.cacheColumns / layout.columns - 1 || index >= 10)
                        value.positionKnown = false;
                    else {
                        value.page = 5; value.stashPage = index + 1; value.sharedStash = false;
                        value.x = uint8_t(x % layout.columns); value.y = uint8_t(y % layout.rows);
                    }
                }
            }
            item->second.revision = ++w.itemRevision;
            break;
        }
        ++w.ignoredPackets;
        ++v.gameProtocol.unconsumed[p.id];
        break;
    }
    default: return false;
    }
    return true;
}
} // namespace
void apply_world_packet(OnlineView &v, const protocol::Packet &p, protocol::ClientProfile profile, const OnlineStashLayout &layout,
                        std::optional<uint16_t> playerDefaultSkill) {
    auto &w = v.world;
    const auto previousItems = w.itemRevision;
    ++v.revision;
    ++w.revision;
    if (apply_social_packet(v, p) && p.id != 0x5C) return;
    const bool pd2 = profile == protocol::ClientProfile::Pd2CnNormal20261020;
    if (pd2 && applyStoragePacketPd2(v, p, layout)) {
        invalidateCubeHostPd2(v);
        return;
    }
    Reader r(p.body);
    switch (p.id) {
    case 0x47:
    case 0x48: {
        // Retail D2Client RVA 0xAE2B0 / 0xAE260 both look up this unit
        // and call 0x82FE0 to refresh equipment eligibility. Neither sends ACK.
        // D2MOO SCmd emits these notifications after inventory publication.
        const auto type = r.u8();
        r.u8();
        const auto id = r.u32();
        r.u32(); r.finish();
        if (const auto owner = w.units.find({type, id}); owner != w.units.end()) {
            ++owner->second.appearanceRevision;
            ++w.itemRevision;
        }
        // Full native eligibility recomputation is not implemented for CN.
        ++w.ignoredPackets;
        ++v.gameProtocol.unconsumed[p.id];
        break;
    }
    case 0x7E:
        // Retail D2Client RVA 0xAD240 opens a local scrolling text resource;
        // it does not read this four-byte payload or alter world/admission state.
        r.u32(); r.finish();
        ++w.ignoredPackets;
        ++v.gameProtocol.unconsumed[p.id];
        break;
    case 0x7D: {
        const auto owner = key(r); const auto itemId = r.u32();
        const auto flag = r.u32(), enabled = r.u32(); r.finish();
        // InvMode -> SCmd::sub_6FC3EFC0; retail AE1F0 -> 84DF0.
        // BROKEN toggles its bit; REPAIRED clears BROKEN rather than
        // installing the transient server-side REPAIRED notification bit.
        if (flag != 0x100 && flag != 0x200) {
            ++w.ignoredPackets; ++v.gameProtocol.unconsumed[p.id]; break;
        }
        const auto actor = w.units.find(owner);
        if (actor == w.units.end()) break;
        const auto updatedFlags = [&](uint32_t flags) {
            return flag == 0x100 && enabled ? flags | 0x100u : flags & ~0x100u;
        };
        bool changed = false;
        const auto item = w.items.find(itemId);
        const bool ownedItem = item != w.items.end() && item->second.ownerType == owner.type &&
            item->second.owner == owner.id;
        if (ownedItem) {
            const auto flags = updatedFlags(item->second.flags);
            changed = flags != item->second.flags;
            item->second.flags = flags;
        }
        if (const auto equipment = w.equipment.find(itemId); equipment != w.equipment.end() &&
            equipment->second.ownerType == owner.type && equipment->second.owner == owner.id) {
            const auto flags = updatedFlags(equipment->second.flags);
            changed |= flags != equipment->second.flags;
            equipment->second.flags = flags;
        }
        if (changed) {
            ++w.itemRevision;
            if (ownedItem) item->second.revision = w.itemRevision;
            ++actor->second.appearanceRevision;
        }
        break;
    }
    case 0x04:
        r.finish();
        // Keep 04 in the ordered map stream. Its receipt does not imply that
        // the MPQ-backed consumer has processed the preceding 07/15 yet.
        mapEvent(w,OnlineMapEvent::Kind::LoadComplete);
        break;
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
        w.questAlerts.erase(k);
        const bool stashRemoved = k.type == 2 &&
            ((w.storage.kind == OnlineStorageKind::Stash && w.storage.source == k.id) ||
             (w.storage.requested == OnlineStorageKind::Stash && w.storage.requestedSource == k.id));
        if ((k.type == 2 && (w.waypointSource == k.id || w.waypointRequested == k.id)) || stashRemoved ||
            (k.type == 1 && (w.npcRequested == k.id || w.shopRequested == k.id || w.shopSource == k.id ||
                (w.npcConversation && w.npcConversation->source == k.id))))
            ++w.interactionGeneration;
        if (k.type == 4) {
            const auto item = w.items.find(k.id);
            const bool remoteEquipment = item != w.items.end() && item->second.mode == 1 &&
                item->second.ownerType == 0 && item->second.owner && item->second.owner != v.load.playerUnitId;
            if (!remoteEquipment) removeItem(w, k.id);
        }
        if (k.type == 2 && w.waypointSource == k.id) w.waypointSource.reset();
        if (k.type == 2 && w.waypointRequested == k.id) w.waypointRequested.reset();
        if (k.type == 2 && w.waypointActivation && w.waypointActivation->source == k.id) w.waypointActivation.reset();
        if (stashRemoved) w.storage = {};
        if(k.type==2 && w.staffSource==k.id) {w.staffSource.reset();++w.interactionGeneration;}
        if(k.type==2 && w.npcConversation && w.npcConversation->type==2 && w.npcConversation->source==k.id) {w.npcConversation.reset();++w.interactionGeneration;}
        if (k.type == 1) {
            if (w.npcRequested == k.id) {w.npcRequested.reset(); w.npcPendingConversation.reset();}
            if (w.npcConversation && w.npcConversation->source == k.id) w.npcConversation.reset();
            if (w.shopRequested == k.id) w.shopRequested.reset();
            if (w.shopSource == k.id) w.shopSource.reset();
        }
        if(k.type==1) removeRemotePlayerItems(w,k.id,1);
        if (k.type == 0) {
            w.social.hover.erase(k.id);
            if (w.playerTrade.peer == k.id) { ++w.interactionGeneration; w.playerTrade = {}; }
            std::erase_if(w.equipment, [&](const auto &e) { return e.second.ownerType==0 && e.second.owner == k.id; });
            if (v.load.playerUnitId == k.id)
                removePlayer(w);
            else removeRemotePlayerItems(w, k.id);
        }
        break;
    }
    case 0x0B: {
        const auto k = key(r);
        r.finish();
        if(k.type==1) removeRemotePlayerItems(w,k.id,1);
        if (k.type == 0) {
            auto &u = unit(w, k);
            if (v.load.playerUnitId == k.id) {
                for (auto &[id, item] : w.items)
                    if (item.ownerType == 0 && !item.owner) { item.owner = k.id; item.revision = ++w.itemRevision; }
                w.playerSkills = u.skills;
                w.playerBaseSkills = u.baseSkills; w.playerBonusSkills = u.bonusSkills;
                w.playerBaseSkillsAssigned = u.baseSkillsAssigned;
                w.itemSkillQuantities = u.itemSkillQuantities;
                w.leftSkill = u.leftSkill;
                w.rightSkill = u.rightSkill;
            }
            if (v.load.playerUnitId == k.id && u.position)
                playerPosition(w, *u.position);
            else if (v.load.playerUnitId == k.id && w.playerPosition)
                position(v, u, *w.playerPosition);
            playerMode(v, u);
        }
        break;
    }
    case 0x0C: {
        auto &u = unit(w, key(r));
        const auto flags = r.u8(); u.hitClass = r.u8(); u.lifePercent = r.u8(); u.lifeCarriesTriggerFlag = u.key.type == 1; r.finish();
        u.hitRevision = w.revision;
        OnlineCombatEvent event; event.packet = p.id; event.kind = OnlineCombatEvent::Kind::Hit;
        event.source = u.key; event.hitClass = u.hitClass; event.life = u.lifePercent; event.flags = flags;
        combatEvent(w, std::move(event));
        break;
    }
    case 0x11: {
        const auto source = key(r); const auto overlay = r.u16(); r.finish();
        OnlineCombatEvent event; event.packet = p.id; event.kind = OnlineCombatEvent::Kind::Overlay;
        event.source = source; event.overlay = overlay;
        combatEvent(w, std::move(event)); // This packet is an overlay, not a kill acknowledgement.
        break;
    }
    case 0x0D: {
        auto &u = unit(w, key(r));
        const auto action = r.u8();
        if (u.key.type == 0 && action == 19) {
            // PlrMsg life/correction update does not select a player mode.
            position(v,u,point(r));u.hitClass=r.u8();u.lifePercent=r.u8();
            u.lifeCarriesTriggerFlag=false;u.hitRevision=w.revision;r.finish();
            OnlineCombatEvent event; event.packet=p.id;event.kind=OnlineCombatEvent::Kind::Hit;
            event.source=u.key;event.hitClass=u.hitClass;event.life=u.lifePercent;
            combatEvent(w,std::move(event));
            break;
        }
        u.mode = action;
        u.nativeMode = false; u.actionSkill.reset(); u.actionSkillLevel.reset();
        position(v, u, point(r));
        u.hitClass = r.u8();
        u.lifePercent = r.u8(); u.lifeCarriesTriggerFlag = false;
        u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds();
        r.finish();
        u.destination.reset();
        u.destinationUnit.reset();
        playerMode(v, u);
        if (u.key.type == 0) {
            OnlineCombatEvent event; event.packet = p.id; event.kind = OnlineCombatEvent::Kind::Action;
            event.source = u.key; event.action = u.mode;
            combatEvent(w, std::move(event));
        }
        break;
    }
    case 0x0E: {
        auto &u = unit(w, key(r));
        const bool wasNeutral=u.mode && *u.mode==0;
        const auto changes = r.u8(), flags = r.u8();
        if (u.key.type == 2 && changes == 3) u.objectTargetable = (flags & 2) != 0;
        const auto mode = r.u32();
        const bool restart = u.key.type != 2 || !u.mode || *u.mode != mode;
        if (mode <= 255)
            u.mode = uint8_t(mode);
        u.nativeMode = true; u.actionSkill.reset(); u.actionSkillLevel.reset();
        u.destination.reset(); u.destinationUnit.reset();
        // Object flags can be refreshed in the same mode. Preserve its original
        // animation epoch so targetability updates cannot restart OP/ENDANIM.
        if (restart) {
            u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds();
        }
        r.finish();
        // Native operation23 on a neutral waypoint only starts OP; it does
        // not send0x63. Treat that actual transition as activation completion,
        // so both original D2GS and the host permit a later menu click.
        if(u.key.type==2 && w.waypointRequested==u.key.id && wasNeutral && mode==1) {
            w.waypointRequested.reset();++w.interactionGeneration;
            w.waypointActivation=OnlineWorldView::WaypointActivation{u.key.id,w.interactionGeneration};
        }
        playerMode(v, u);
        break;
    }
    case 0x0F: {
        auto &u = unit(w, key(r));
        u.mode = r.u8();
        u.nativeMode = false; u.actionSkill.reset(); u.actionSkillLevel.reset();
        u.destination = point(r);
        u.destinationUnit.reset();
        r.u8();
        position(v, u, point(r));
        u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds();
        r.finish();
        break;
    }
    case 0x10: {
        auto &u = unit(w, key(r));
        u.mode = r.u8();
        u.nativeMode = false; u.actionSkill.reset(); u.actionSkillLevel.reset();
        u.destinationUnit = key(r);
        position(v, u, point(r));
        u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds();
        r.finish();
        u.destination.reset();
        break;
    }
    case 0x15: {
        auto &u = unit(w, key(r));
        position(v, u, point(r));
        r.u8();
        ++u.positionDiscontinuity; // Native correction/teleport must not tween across geometry.
        u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds();
        r.finish();
        u.destination.reset();
        u.destinationUnit.reset();
        if (u.key.type == 0 && v.load.playerUnitId == u.key.id) w.movementRequest.reset();
        if (u.key.type == 0 && v.load.playerUnitId == u.key.id &&
            w.respawnRequest && w.respawnRequest->sent) {
            w.respawnRequest->repositioned = true;
            resurrectionUpdates(v);
        }
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
    case 0x19: {
        const auto increment = r.u8(); r.finish();
        auto &gold = w.playerAttributes[14];
        if (increment > UINT32_MAX - gold) throw ProtocolError("Gold increment overflow");
        gold += increment;
        break;
    }
    case 0x1A:
    case 0x1B:
    case 0x1C: {
        const auto value = p.id == 0x1A ? uint32_t(r.u8()) : p.id == 0x1B ? uint32_t(r.u16()) : r.u32();
        r.finish();
        if (p.id == 0x1C) w.playerAttributes[13] = value;
        else {
            // PlrMsg starts this client's experience baseline at zero. Its
            // first update may already be a compact delta, not an absolute
            // stat. Original 1.13c SCmd RVA ABDA0 writes the difference for
            // 1A/1B and the full value for 1C (D2MOO's assignments differ).
            auto &experience = w.playerAttributes[13];
            if (value > UINT32_MAX - experience) throw ProtocolError("Experience increment overflow");
            experience += value;
        }
        break;
    }
    case 0x1D:
    case 0x1E:
    case 0x1F: {
        const auto attribute = r.u8();
        w.playerAttributes[attribute] = p.id == 0x1D ? r.u8() : p.id == 0x1E ? r.u16() : r.u32();
        r.finish();
        // A later absolute resource stat supersedes an older compact sample.
        // UI applies MPQ ValShift to the stat; transport must not truncate it
        // into the compact packet's different whole-point representation.
        if (attribute == 6) w.life.reset();
        else if (attribute == 8) w.mana.reset();
        else if (attribute == 10) w.stamina.reset();
        if (w.respawnRequest && w.respawnRequest->sent) {
            if (attribute == 6 && w.playerAttributes[attribute] > 0) w.respawnRequest->restoredResources |= 1;
            else if (attribute == 8) w.respawnRequest->restoredResources |= 2;
            else if (attribute == 10) w.respawnRequest->restoredResources |= 4;
            resurrectionUpdates(v);
        }
        break;
    }
    case 0x20: {
        auto &entry = unit(w, {0, r.u32()});
        // The current 1.13c length table has a byte stat ID. D2MOO's
        // extended PacketStatId is a word and must not be used on this wire.
        const auto attribute = r.u8();
        entry.attributes[attribute] = std::bit_cast<int32_t>(r.u32());
        r.finish();
        break;
    }
    case 0x21: {
        const auto type = r.u8(); r.u8(); const auto owner = r.u32();
        const auto skill = r.u16(); const auto base = r.u8(), bonus = r.u8(); r.u8(); r.finish();
        auto &entry = unit(w, {type, owner});
        entry.baseSkills[skill] = base; entry.bonusSkills[skill] = bonus;
        entry.skills[skill] = uint16_t(base) + bonus;
        if (type == 0 && v.load.playerUnitId == owner) {
            w.playerBaseSkills[skill] = base; w.playerBonusSkills[skill] = bonus;
            w.playerSkills[skill] = entry.skills[skill];
        }
        break;
    }
    case 0x22: {
        r.u8(); r.u8();
        const auto owner = r.u32();
        const auto skill = r.u16(); const auto quantity = r.u8();
        r.u8(); const auto flags = r.u8(); r.finish();
        if (flags != 0) break;
        const auto found = w.units.find({0, owner});
        if (found == w.units.end()) break;
        found->second.itemSkillQuantities[skill] = quantity;
        if (v.load.playerUnitId == owner)
            w.itemSkillQuantities[skill] = quantity;
        break;
    }
    case 0x3F: {
        const auto cursor = r.u8(); const auto source = r.u32(); r.u16(); r.finish();
        ++w.itemTargetingRevision;w.itemTargetingCursor=cursor==255?-1:int(cursor);
        if (cursor == 255) w.itemTargetingSource.reset();
        else w.itemTargetingSource = source;
        break;
    }
    case 0x7C: {
        const auto target = key(r); r.finish();
        if (target.type > 5) throw ProtocolError("Invalid item spell completion unit type");
        // Retail AD2A0 -> 81AA0 removes the named unit's uninterruptable
        // stat list (state 54). ItemMode/SkillItem send this for both a
        // completed scroll and a failed pSpell; it is not item consumption
        // or an acknowledgement that a portal was created.
        if (auto found = w.units.find(target); found != w.units.end()) {
            OnlineStateMessage message;
            message.kind = OnlineStateMessage::Kind::Disable;
            message.state = 54;
            stateMessage(found->second, std::move(message));
        }
        // Item stat lists have no unit-state projection here. Their quantity,
        // removal and targeting cursor still follow 22/9C/9D/3F respectively.
        break;
    }
    case 0x28: case 0x29: {
        uint8_t kind = 6;
        uint32_t source=0;
        if (p.id == 0x28) { kind = r.u8(); source=r.u32(); r.u8(); }
        std::array<uint16_t, 48> flags;
        for (auto &value : flags) value = r.u16();
        r.finish();
        if (p.id == 0x29) w.quests.gameFlags = flags;
        else if (kind == 6 || kind == 1) w.quests.playerFlags = flags; // Initialization or NPC's player-private record.
        if(p.id==0x28 && kind==1 && w.npcRequested==source && w.npcPendingConversation &&
            w.npcPendingConversation->source==source && w.units.contains({1,source})) {
            w.npcConversation=std::move(w.npcPendingConversation);
            w.npcPendingConversation.reset();
            w.npcConversation->revision=w.revision;
        }
        w.quests.revision = w.revision;
        break;
    }
    case 0x5E: {
        std::array<uint8_t,37> availability;
        for(auto &value:availability) value=r.u8();
        r.finish(); w.quests.availability=availability; w.quests.revision=w.revision;
        break;
    }
    case 0x92: {
        // PlrTrade restores a saved inventory under new item identities. The
        // reset also reaches observers, whose public equipment must be cleared.
        const auto owner = key(r); r.finish();
        removeRemotePlayerItems(w,owner.id,owner.type);
        if (owner.type == 0 && owner.id == v.load.playerUnitId) {
            ++w.interactionGeneration;
            w.storage = {};
            if (w.itemRequest && w.itemRequest->state == OnlineItemRequest::State::Pending)
                w.itemRequest->state = OnlineItemRequest::State::Interrupted;
            w.itemTargetingSource.reset(); ++w.itemTargetingRevision;
        }
        break;
    }
    case 0x91: {
        const auto act = r.u8();
        std::array<uint16_t,12> introductions;
        for (auto &npc : introductions) npc = r.u16();
        r.finish();
        // QUESTS_UpdateNPCIntros: NPC class IDs or FFFF, not quest rewards.
        // Keep the report for the MPQ-backed dialogue consumer; the native
        // client only sets listed intro flags and never clears absent slots.
        if (act >= w.quests.npcIntroductionReports.size()) {
            ++w.ignoredPackets; ++v.gameProtocol.unconsumed[p.id]; break;
        }
        w.quests.npcIntroductionReports[act] = introductions;
        for (const auto npc : introductions) if (npc != UINT16_MAX) w.quests.npcReturnGreetings.insert(npc);
        w.quests.revision = w.revision;
        break;
    }
    case 0x5F:
        w.playerPortalFlags=r.u32(); r.finish(); break;
    case 0x52:
        for (auto &status : w.quests.statuses) status = r.u8();
        r.finish(); w.quests.revision = w.revision; break;
    case 0x5D: {
        const auto id = r.u8(), flags = r.u8(), status = r.u8(); const auto progress = r.u16(); r.finish();
        w.quests.updates[id] = {flags, status, progress};
        // Quest numbers in 0x5D differ from the record/filter indices used by 0x52.
        const int slot = id <= 6 ? id : id >= 7 && id <= 13 ? id+1 : id >= 14 && id <= 20 ? id+2 :
            id >= 21 && id <= 24 ? id+3 : id >= 31 && id <= 36 ? id+4 : -1;
        if (slot >= 0) w.quests.statuses[size_t(slot)] = status;
        if (id == 1) w.quests.denRemaining = progress;
        if (id == 32) w.quests.rescuedBarbsRemaining = progress;
        w.quests.revision = w.revision; break;
    }
    case 0x50: {
        const auto id = r.u16();
        std::array<uint16_t, 6> payload; for (auto &value : payload) value = r.u16(); r.finish();
        if (id == 1) {
            w.quests.denRemaining = payload[0]; w.quests.staffTombOffset = payload[1];
            w.quests.rescuedBarbsRemaining = payload[2]; w.quests.revision = w.revision;
        } else if(id==13) {
            if(payload[0]>=7) throw net::protocol::ProtocolError("Invalid native staff tomb offset");
            w.quests.staffTombOffset=payload[0];w.quests.revision=w.revision;
        } else if(id==4) {
            std::array<int,5> stones{};unsigned seen=0;
            for(size_t index=0;index<stones.size();++index) {
                const auto value=payload[index];
                if(value>=5 || (seen&(1u<<value))) throw ProtocolError("Invalid native Cairn Stone order");
                seen|=1u<<value;stones[index]=17+value;
            }
            w.quests.cainStones=stones;w.quests.cainStonesRevision=w.revision;w.quests.revision=w.revision;
        } else if(id==36) {
            w.quests.zooMonster=payload[0];w.quests.revision=w.revision;
        }
        break;
    }
    case 0x53: {const auto cycle=r.u32();r.u32();const auto tainted=r.u8();r.finish();if(cycle<=5) w.eclipse=tainted!=0;break;}
    case 0x58: {
        const auto source=r.u32();const auto result=r.u8();r.u8();r.finish();
        if(result==0) {w.staffSource=source;w.staffResult=result;++w.staffRevision;}
        else if(w.staffSource==source && (result==1 || result==4 || result==5 || result==7)) {w.staffResult=result;++w.staffRevision;if(result!=4)w.staffSource.reset();}
        break;
    }
    case 0x7A: {
        const auto assign=r.u8(),type=r.u8();const auto monsterClass=r.u16();const auto owner=r.u32(),id=r.u32();r.finish();
        if(assign) {
            if(!w.pets.contains(id) && w.pets.size()>=8192) throw ProtocolError("Remote pet limit exceeded");
            const auto previous=w.pets.find(id);
            if(previous==w.pets.end() || previous->second.type!=type || previous->second.monsterClass!=monsterClass || previous->second.owner!=owner)
                w.pets.insert_or_assign(id,OnlinePet{type,monsterClass,owner,{},{}});
        }
        else w.pets.erase(id);
        break;
    }
    case 0x7B: {
        const auto slot = r.u8(); const auto packed = r.u16(); const auto owner = r.u32(); r.finish();
        if (slot >= w.skillHotkeys.size()) throw ProtocolError("Invalid native hotkey slot");
        OnlineSkillHotkey hotkey;
        hotkey.hand = packed & 0x8000 ? OnlineSkillHand::Left : OnlineSkillHand::Right;
        if ((packed & 0x0FFF) != 0x0FFF) hotkey.selection = OnlineSkillSelection{uint16_t(packed & 0x0FFF), owner};
        w.skillHotkeys[slot] = hotkey;
        break;
    }
    case 0x23: {
        const auto target = key(r);
        const auto left = r.u8();
        const auto skill = r.u16(); const auto owner = r.u32(); r.finish();
        if (target.type == 0) {
            auto &entry = unit(w, target);
            if (owner != UINT32_MAX && v.load.playerUnitId == target.id) {
                // Native active-skill assignment leaves the old hand intact
                // when no skill node matches its item owner. Fresh characters
                // can receive zeroed saved-slot owners after a valid start skill.
                // A real item GUID 0 remains eligible when actually assigned.
                // Other players' private item sources are not observable here.
                const auto source = w.items.find(owner);
                if (source == w.items.end() || source->second.ownerType != 0 ||
                    source->second.owner != target.id || source->second.mode != 1) break;
            }
            (left ? entry.leftSkill : entry.rightSkill) = OnlineSkillSelection{skill, owner};
            if (v.load.playerUnitId == target.id)
                (left ? w.leftSkill : w.rightSkill) = OnlineSkillSelection{skill, owner};
        }
        break;
    }
    case 0x27: {
        const auto target = key(r);
        const auto count = r.u8(); r.u8();
        if (count >= 8) throw ProtocolError("Invalid native NPC message count");
        std::vector<OnlineNpcMessage> messages;
        for (int i = 0; i < 8; ++i) {
            const auto menu = r.u8(); r.u8(); const auto id = r.u16();
            if (i < count) messages.push_back({id, menu});
        }
        r.finish();
        if ((target.type == 1 && w.npcRequested == target.id) || (target.type==2 && w.units.contains(target))) {
            w.questAlerts.erase(target);
            // A new conversation waits for NPC-context 28. Native speech
            // refreshes use 27/29 only and replace the already active list.
            const bool active=target.type==1 && w.npcConversation && w.npcConversation->source==target.id;
            auto &current=target.type==1 && !active?w.npcPendingConversation:w.npcConversation;
            if (!current || current->source != target.id)
                current = OnlineNpcConversation{target.id, 0, {}, {}};
            auto &conversation = *current;
            conversation.type=target.type;
            conversation.messages = std::move(messages);
            conversation.acknowledged.clear();
            conversation.revision = w.revision;
        }
        break;
    }
    case 0x8A: {
        const auto target = key(r); r.finish();
        if (target.type == 1 && w.questAlerts.size() < 4096) w.questAlerts.insert(target);
        break;
    }
    case 0x2C: {
        OnlineCombatEvent event; event.packet = p.id; event.kind = OnlineCombatEvent::Kind::Sound;
        event.source = key(r); event.auxiliary = r.u16(); r.finish();
        if(event.source.type>=0xf0) {
            event.source.type=uint8_t(event.source.type-0xf0);
            if(event.source.type>=9) break;
            event.directSound=true;
        }
        combatEvent(w, std::move(event));
        break;
    }
    case 0x4F: {
        r.finish();
        auto &service=w.hirelingService;
        if(service.source && w.npcConversation && service.source==w.npcConversation->source && service.interaction==w.interactionGeneration) {
            service.offers.clear();service.listReceived=true;service.revision=w.revision;
            if(service.pending==OnlineHirelingAction::List) service.pending.reset();
        }
        break;
    }
    case 0x4E: {
        const auto name=r.u16();const auto seed=r.u32();r.finish();
        auto &service=w.hirelingService;
        if(service.listReceived && service.source && w.npcConversation && service.source==w.npcConversation->source &&
            service.interaction==w.interactionGeneration && service.offers.size()<64) {
            service.offers.insert_or_assign(name,seed);service.revision=w.revision;
        }
        break;
    }
    case 0x81: {
        const auto type=r.u8();const auto monsterClass=r.u16();const auto owner=r.u32(),id=r.u32(),seed=r.u32(),name=r.u32();r.finish();
        if(type==7 && owner==v.load.playerUnitId) {
            if(!w.hireling || w.hireling->id!=id) w.hireling=OnlineHireling{id,owner,seed,name,monsterClass,{}};
            else {w.hireling->seed=seed;w.hireling->name=name;w.hireling->monsterClass=monsterClass;}
            // PlayerPets uses 0x81 instead of 0x7A to assign the owner's hireable.
            if (!w.pets.contains(id) && w.pets.size()>=8192) throw ProtocolError("Remote pet limit exceeded");
            const auto previous=w.pets.find(id);
            if(previous==w.pets.end() || previous->second.type!=type || previous->second.monsterClass!=monsterClass || previous->second.owner!=owner)
                w.pets.insert_or_assign(id,OnlinePet{type,monsterClass,owner,{},{}});
        }
        break;
    }
    case 0x9E: case 0x9F: case 0xA0: case 0xA1: case 0xA2: {
        const auto stat=r.u8();const auto id=r.u32();
        const uint32_t value=p.id==0x9E || p.id==0xA1?r.u8():p.id==0x9F || p.id==0xA2?r.u16():r.u32();r.finish();
        if(w.hireling && w.hireling->id==id) {
            if(p.id==0xA1 || p.id==0xA2) {
                const auto previous=w.hireling->attributes.find(stat);
                if(previous!=w.hireling->attributes.end() && value<=UINT32_MAX-previous->second) previous->second+=value;
            } else w.hireling->attributes[stat]=value;
        }
        break;
    }
    case 0x9B: {
        const auto name=r.u16();const auto cost=r.u32();r.finish();
        w.deadHirelingName=name==UINT16_MAX?std::nullopt:std::optional{name};
        w.hirelingReviveCost=name==UINT16_MAX?std::nullopt:std::optional{cost};
        break;
    }
    case 0x2A: {
        OnlineTradeResult result;
        result.flags = r.u8(); result.result = r.u8(); r.u32();
        result.item = r.u32(); result.gold = r.u32(); r.finish();
        result.revision = w.revision; w.tradeResult = result;
        auto &service=w.hirelingService;
        if(service.pending && *service.pending!=OnlineHirelingAction::List && service.interaction==w.interactionGeneration) {
            service.result=result.result;service.pending.reset();service.revision=w.revision;
            if(result.result==5) {service.offers.clear();service.listReceived=false;}
        }
        // Wallet replication uses 0x19 / 0x1D-E-F. Applying this receipt as well
        // would count a following positive gold increment twice on a sale.
        // Sale sends REMOVEFROMCONTAINER with the old stored mode. The explicit
        // transaction reply transfers its identity out of the player's inventory.
        if (result.result == 1)
            if (const auto item = w.items.find(result.item); item != w.items.end() &&
                item->second.ownerType == 0 && item->second.owner == v.load.playerUnitId)
                removeItem(w, result.item);
        break;
    }
    case 0x77: {
        const auto action = r.u8(); r.finish();
        auto &trade = w.playerTrade;
        if (action == 0 || action == 1) {
            trade = {};
            trade.phase = action == 0 ? OnlinePlayerTrade::Phase::Outgoing : OnlinePlayerTrade::Phase::Incoming;
            trade.revision = w.revision; trade.lastAction = action;
            ++w.interactionGeneration;
            w.movementRequest.reset();
            w.npcRequested.reset(); w.npcPendingConversation.reset(); w.npcConversation.reset();
            w.waypointRequested.reset(); w.waypointSource.reset();
            w.storage = {}; w.shopRequested.reset(); w.shopSource.reset(); w.shopGamble = false;
            if (w.itemRequest && w.itemRequest->state == OnlineItemRequest::State::Pending)
                w.itemRequest->state = OnlineItemRequest::State::Interrupted;
        } else if (action == 6 && trade.active()) {
            if (trade.phase != OnlinePlayerTrade::Phase::Open) ++w.interactionGeneration;
            trade.phase = OnlinePlayerTrade::Phase::Open;
            if (trade.response != OnlinePlayerTrade::Response::GoldSent)
                trade.response = OnlinePlayerTrade::Response::None;
            trade.ownAgreed = trade.peerAgreed = false;
            trade.revision = w.revision; trade.lastAction = action;
        } else if (action == 12 || action == 13) {
            if (trade.active()) ++w.interactionGeneration;
            trade = {}; trade.revision = w.revision; trade.lastAction = action;
        } else if (trade.active() && (action == 5 || action == 9 || action == 10 || action == 14 || action == 15)) {
            if (action == 5) trade.peerAgreed = true;
            if (action == 14 || action == 15) trade.agreementLocked = action == 14;
            if (action == 9 || action == 10) trade.ownAgreed = trade.peerAgreed = false;
            trade.revision = w.revision; trade.lastAction = action;
        }
        if ((action == 16 && w.storage.requested == OnlineStorageKind::Stash) ||
            (action == 21 && w.storage.requested == OnlineStorageKind::Cube)) {
            w.storage.kind = w.storage.requested; w.storage.source = w.storage.requestedSource;
            w.storage.requested = OnlineStorageKind::None; w.storage.requestedSource.reset();
            w.storage.revision = w.revision;
            if (pd2) invalidateCubeHostPd2(v);
        } else if (action == 17 && w.storage.kind == OnlineStorageKind::Stash) {
            ++w.interactionGeneration;
            w.storage.kind = OnlineStorageKind::None; w.storage.source.reset(); w.storage.revision = w.revision;
        }
        break;
    }
    case 0x78: {
        const auto peerName = name(r);
        const auto peer = r.u32(); r.finish();
        if (peer == UINT32_MAX || peer == v.load.playerUnitId || peerName.empty() ||
            !std::all_of(peerName.begin(), peerName.end(), [](unsigned char c) { return c >= 32 && c < 127; }))
            throw ProtocolError("Invalid trade player identity");
        if (w.playerTrade.phase == OnlinePlayerTrade::Phase::Open) {
            w.playerTrade.peer = peer; w.playerTrade.peerName = peerName;
            w.playerTrade.revision = w.revision;
        } else ++w.ignoredPackets;
        break;
    }
    case 0x79: {
        const auto side = r.u8(); const auto amount = r.u32(); r.finish();
        if (side > 1) throw ProtocolError("Invalid player trade gold side");
        auto &trade = w.playerTrade;
        if (trade.phase != OnlinePlayerTrade::Phase::Open) { ++w.ignoredPackets; break; }
        (side ? trade.ownGold : trade.peerGold) = amount;
        if (side && trade.response == OnlinePlayerTrade::Response::GoldSent)
            trade.response = OnlinePlayerTrade::Response::None;
        trade.revision = w.revision;
        break;
    }
    case 0x4C:
    case 0x99: {
        OnlineCombatEvent event; event.packet = p.id; event.kind = OnlineCombatEvent::Kind::Skill;
        event.source = key(r); event.skill = r.u16(); event.level = r.u8(); event.target = key(r);
        event.auxiliary = r.u16(); r.finish(); combatEvent(w, std::move(event));
        break;
    }
    case 0x4D:
    case 0x9A: {
        OnlineCombatEvent event; event.packet = p.id; event.kind = OnlineCombatEvent::Kind::Skill;
        event.source = key(r); event.skill = uint16_t(r.u32()); event.level = r.u8(); event.point = point(r);
        event.auxiliary = r.u16(); r.finish(); combatEvent(w, std::move(event));
        break;
    }
    case 0xA5: {
        OnlineCombatEvent event;event.packet=p.id;event.kind=OnlineCombatEvent::Kind::Skill;
        event.source=key(r);event.skill=r.u16();r.finish();combatEvent(w,std::move(event));
        break;
    }
    case 0x89: {
        const auto effect=r.u8();r.finish();
        if(effect<20) {
            w.questEffects|=uint32_t{1}<<effect;
            if(w.questEffectOccurrences[effect]==UINT64_MAX) throw ProtocolError("Quest effect sequence exhausted");
            ++w.questEffectOccurrences[effect];
        }
        // Retail effect18 only sets this bit; its immediate callback is null.
        break;
    }
    case 0xA3: {
        OnlineCombatEvent event; event.packet = p.id; event.kind = OnlineCombatEvent::Kind::Skill;
        event.flags = r.u8(); event.skill = r.u16(); event.level = r.u16();
        event.source = key(r); event.target = key(r);
        if(event.target->id==UINT32_MAX) event.target.reset();
        const auto x = r.u32(), y = r.u32(); r.finish();
        event.skillArguments=std::array{x,y};
        if (x <= UINT16_MAX && y <= UINT16_MAX) event.point = OnlinePoint{uint16_t(x), uint16_t(y)};
        combatEvent(w, std::move(event));
        break;
    }
    case 0x73: {
        OnlineCombatEvent event; event.packet = p.id; event.kind = OnlineCombatEvent::Kind::Missile;
        event.flags = r.u32(); event.missile = r.u16();
        const auto x = r.u32(), y = r.u32(), firstX = r.u32(), firstY = r.u32();
        event.auxiliary = r.u16(); event.source = key(r); event.level = r.u8(); event.pierce = r.u8(); r.finish();
        if (x <= UINT16_MAX && y <= UINT16_MAX) event.point = OnlinePoint{uint16_t(x), uint16_t(y)};
        // The protocol carries a missile class and owner, not an authoritative missile GUID.
        event.missileDestination = std::array{firstX, firstY};
        combatEvent(w, std::move(event));
        break;
    }
    case 0xA7:
    case 0xA9: {
        auto &entry = unit(w, key(r));
        OnlineStateMessage message; message.kind = p.id == 0xA7 ? OnlineStateMessage::Kind::Enable
            : OnlineStateMessage::Kind::Disable;
        message.state = r.u8(); r.finish(); stateMessage(entry, std::move(message));
        break;
    }
    case 0xA8:
    case 0xAA: {
        auto &entry = unit(w, key(r));
        if (r.u8() != p.body.size() + 1) throw ProtocolError("Invalid native state message length");
        OnlineStateMessage message; message.kind = p.id == 0xAA ? OnlineStateMessage::Kind::Snapshot
            : OnlineStateMessage::Kind::Enable;
        if (p.id == 0xA8) message.state = r.u8();
        const auto packed = r.take(r.remaining()); message.packed.assign(packed.begin(), packed.end());
        stateMessage(entry, std::move(message));
        break;
    }
    case 0x51: {
        auto &u = unit(w, key(r));
        u.classId = r.u16();
        u.equipmentObserved=true;
        position(v, u, point(r));
        u.mode = r.u8();
        u.objectInteractType = r.u8();
        u.objectTargetable.reset();
        u.nativeMode = true; u.actionSkill.reset(); u.actionSkillLevel.reset();
        u.destination.reset(); u.destinationUnit.reset();
        u.assignmentRevision = w.revision;
        u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds();
        r.finish();
        break;
    }
    case 0x59: {
        auto &u = unit(w, {0, r.u32()});
        u.classId = r.u8();
        if (*u.classId >= 7) throw ProtocolError("Invalid assigned player class");
        u.name = name(r);
        // SKILLS_InitSkillList installs Attack in empty hand slots before
        // the server's saved/equipment selections arrive. Identity is prepared
        // from this client's MPQ; do not guess a class's starting spell.
        if (playerDefaultSkill) {
            if (!u.leftSkill) u.leftSkill = OnlineSkillSelection{*playerDefaultSkill, UINT32_MAX};
            if (!u.rightSkill) u.rightSkill = OnlineSkillSelection{*playerDefaultSkill, UINT32_MAX};
            if (v.load.playerUnitId == u.key.id) {
                w.leftSkill = u.leftSkill;
                w.rightSkill = u.rightSkill;
            }
        }
        u.assignmentRevision = w.revision;
        // SUNITMSG_FirstFn assigns an empty player inventory before sending
        // its equipped items. An empty equipment stream is a valid naked
        // character, not a missing appearance. Do not erase preceding gear.
        u.equipmentObserved = true;
        if (!u.mode || (v.load.playerUnitId == u.key.id && w.respawnRequest && w.respawnRequest->sent &&
                       onlinePlayerDead(w))) {
            u.mode = 7; // Native player assignment starts neutral.
            u.nativeMode = false; u.actionSkill.reset(); u.actionSkillLevel.reset();
            u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds();
        }
        position(v, u, point(r));
        r.finish();
        playerMode(v, u);
        break;
    }
    case 0x74:
    case 0x8E: {
        const auto assign = r.u8(); const auto owner = r.u32(), corpse = r.u32(); r.finish();
        if (assign > 1 || owner == corpse) throw ProtocolError("Invalid native corpse assignment");
        if (assign) {
            if (!w.corpseOwners.contains(corpse) && w.corpseOwners.size() >= 8192)
                throw ProtocolError("Remote corpse limit exceeded");
            w.corpseOwners[corpse] = owner;
        } else w.corpseOwners.erase(corpse);
        break;
    }
    case 0x5C: {
        const auto id = r.u32();
        r.finish();
        if (w.playerTrade.peer == id) { ++w.interactionGeneration; w.playerTrade = {}; }
        w.units.erase({0, id});
        std::erase_if(w.equipment, [&](const auto &e) { return e.second.owner == id; });
        if (v.load.playerUnitId == id)
            removePlayer(w);
        else removeRemotePlayerItems(w, id);
        break;
    }
    case 0x60: {
        const auto flags = r.u8(), destination = r.u8();
        auto &u = unit(w, {2, r.u32()});
        u.portalFlags = flags;
        u.portalDestination = destination;
        r.finish();
        break;
    }
    case 0x63: {
        const auto source = r.u32();
        std::array<uint16_t, 8> history;
        for (auto &word : history) word = r.u16();
        r.finish();
        if (history[0] != 0x102) throw ProtocolError("Unsupported native waypoint history");
        w.waypointHistory = history;
        const bool activated=w.waypointActivation && w.waypointActivation->source==source &&
            w.waypointActivation->generation==w.interactionGeneration;
        if ((w.waypointRequested == source || w.waypointSource == source || activated) &&
            w.playerPosition && !onlinePlayerDead(w) && w.units.contains({2, source})) {
            w.waypointSource = source;
            w.waypointRequested.reset();
        } else ++w.lateWaypointReplies;
        w.waypointActivation.reset();
        break;
    }
    case 0x82: {
        const auto owner = r.u32();
        const auto ownerName = name(r);
        // Both endpoint GUIDs belong to this owner. Do not infer which is local.
        for (int i = 0; i < 2; ++i) {
            const auto id = r.u32();
            if (id == UINT32_MAX) continue;
            auto &u = unit(w, {2, id});
            u.portalOwner = owner;
            u.portalOwnerName = ownerName;
        }
        r.finish();
        break;
    }
    case 0x67:
    case 0x68: {
        auto &u = unit(w, {1, r.u32()});
        monsterAction(w, u, r.u8());
        if (p.id == 0x67) {
            // 0x67 contains a path endpoint; 0x68 contains current position and target GUID.
            u.destination = point(r); u.destinationUnit.reset();
        } else {
            position(v, u, point(r)); u.destinationUnit = key(r); u.destination.reset();
        }
        u.pathSteps = r.u8(); u.hitClass = r.u8(); u.pathType = r.u8();
        u.velocityPercent = int16_t(r.u16()); u.pathDistance = r.u8(); r.finish();
        // Walking overloads send maximum path distance here, not HP. Knockback sends HP.
        if (u.mode == 13) { u.lifePercent = u.pathDistance; u.lifeCarriesTriggerFlag = false; }
        break;
    }
    case 0x69:
    case 0x6A: {
        auto &u = unit(w, {1, r.u32()}); monsterAction(w, u, r.u8());
        OnlineCombatEvent event; event.packet = p.id; event.kind = OnlineCombatEvent::Kind::Action;
        event.source = u.key; event.action = u.wireAction;
        if (p.id == 0x69) {
            const auto coordinates = point(r); event.point = coordinates;
            u.destinationUnit.reset();
            if (u.wireAction == 9) {
                // MonsterMsg DEAD carries the current position, not a path
                // target. Keep the corpse there and discard the old walk goal.
                position(v, u, coordinates); u.destination.reset();
            } else u.destination = coordinates;
            event.direction = r.u8(); event.hitClass = r.u8();
            if(u.wireAction==6) {
                // Retail D2Client RVA 4E095: GH's byte is HP plus the
                // lightning-ready flag, not a facing direction.
                u.lifePercent=*event.direction;u.lifeCarriesTriggerFlag=true;
                event.life=u.lifePercent;event.direction.reset();
                position(v,u,coordinates);u.destination.reset();
            }
        } else {
            u.destinationUnit = key(r); u.destination.reset(); event.target = u.destinationUnit;
            event.direction = r.u8();
        }
        // MonsterMsg supplies path facing only for DEATH/DEAD. Other
        // actions leave this byte zero (GH's HP was consumed above).
        if (u.wireAction != 8 && u.wireAction != 9) event.direction.reset();
        u.direction = event.direction;
        r.finish(); combatEvent(w, std::move(event));
        break;
    }
    case 0x6B:
    case 0x6C: {
        auto &u = unit(w, {1, r.u32()});
        monsterAction(w, u, r.u8());
        OnlineCombatEvent event; event.packet = p.id; event.kind = OnlineCombatEvent::Kind::Action;
        event.source = u.key; event.action = u.wireAction;
        // MonsterMsg leaves the direction byte zero for these targeted
        // actions. Facing comes from the target, not that placeholder.
        if (p.id == 0x6B) {
            u.destination = point(r); u.destinationUnit.reset(); r.u8(); u.hitClass = r.u8();
            event.point = u.destination; event.hitClass = u.hitClass;
        } else {
            u.destinationUnit = key(r); u.destination.reset(); r.u8(); event.target = u.destinationUnit;
        }
        position(v, u, point(r));
        r.finish(); combatEvent(w, std::move(event));
        break;
    }
    case 0x6D: {
        auto &u = unit(w, {1, r.u32()});
        position(v, u, point(r));
        u.lifePercent = r.u8(); u.lifeCarriesTriggerFlag = false;
        r.finish();
        u.mode = 1; u.wireAction = 7; u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds();
        u.actionSkill.reset(); u.actionSkillLevel.reset(); u.direction.reset();
        u.destination.reset();
        u.destinationUnit.reset();
        break;
    }
    case 0x18:
    case 0x95:
    case 0x96: {
        BitReader bits(p.body);
        if (p.id != 0x96) {
            const auto previousLife = w.life;
            w.life = uint16_t(bits.read(15));
            w.mana = uint16_t(bits.read(15));
            // A death save may enter a new game with zero HP and a living NU
            // action. Only an actual positive-to-zero transition supplements
            // the authoritative death modes; initial zero is not a death event.
            if (!*w.life && previousLife && *previousLife > 0) {
                if (w.deathPhase != OnlineDeathPhase::Dying && w.deathPhase != OnlineDeathPhase::Dead) {
                    w.deathPhase = OnlineDeathPhase::Dying; w.deathRevision = w.revision;
                    w.respawnRequest.reset();
                }
                if (w.waypointSource || w.waypointRequested || w.npcRequested || w.npcConversation)
                    ++w.interactionGeneration;
                w.waypointSource.reset(); w.waypointRequested.reset(); w.npcRequested.reset(); w.npcPendingConversation.reset();
                w.waypointActivation.reset();
                w.npcConversation.reset(); w.movementRequest.reset();
                w.townPortalPending = false;
            }
        }
        w.stamina = uint16_t(bits.read(15));
        if (p.id == 0x18) {
            bits.read(7);
            bits.read(7);
        }
        const auto x = uint16_t(bits.read(16)), y = uint16_t(bits.read(16));
        // PlrMsg subtracts PATH_GetFirstPoint (tTargetCoord) from the current
        // coordinates. A detour can shorten this target before sending it.
        const auto signedOffset = [&] { const int value = int(bits.read(8)); return value < 128 ? value : value - 256; };
        const int targetX = int(x) - signedOffset(), targetY = int(y) - signedOffset();
        if (v.load.playerUnitId) {
            auto &player = unit(w, {0, *v.load.playerUnitId});
            position(v, player, {x, y});
            player.verifiedDestination.reset();
            if (targetX >= 0 && targetY >= 0 && targetX <= UINT16_MAX && targetY <= UINT16_MAX)
                player.verifiedDestination = OnlinePoint{uint16_t(targetX), uint16_t(targetY)};
            player.pathVerificationRevision = w.revision;
        } else
            playerPosition(w, {x, y});
        break;
    }
    case 0x94: {
        const auto count = r.u8(); const auto owner = r.u32();
        auto &assigned = unit(w, {0, owner});
        for (int i = 0; i < count; ++i) {
            const auto skill = r.u16(); const auto level = r.u8();
            auto &entry = unit(w, {0, owner}); entry.baseSkills[skill] = level;
            entry.skills[skill] = uint16_t(level) + entry.bonusSkills[skill];
            if (v.load.playerUnitId == owner) {
                w.playerBaseSkills[skill] = level; w.playerSkills[skill] = entry.skills[skill];
            }
        }
        r.finish();
        assigned.baseSkillsAssigned = true;
        if (v.load.playerUnitId == owner) w.playerBaseSkillsAssigned = true;
        break;
    }
    case 0x3E: {
        const auto length = r.u8();
        if (length < 3 || length > 34 || length != p.body.size() + 1)
            throw ProtocolError("Invalid native item stat update length");
        const auto packed = r.take(length - 2); r.finish();
        BitReader bits(packed);
        auto variable = [&] { return bits.read(bits.read(1) ? (bits.read(1) ? 32 : 16) : 8); };
        const auto id = variable();
        const bool base = bits.read(1) != 0;
        const auto stat = uint16_t(bits.read(9));
        const auto value = variable();
        const auto parameter = uint16_t(bits.read(bits.read(1) ? 16 : 8));
        // The original AE300 handler still reads nine bits in this CN build;
        // full-item format selection does not apply to this standalone packet.
        if (pd2 && (stat == 511 || bits.remaining() > 7 || bits.read(unsigned(bits.remaining())) != 0))
            throw ProtocolError("Invalid PD2 incremental item stat or trailing data");
        if (auto found = w.items.find(id); found != w.items.end()) {
            found->second.statUpdates[{stat, parameter}] = {value, base};
            found->second.revision = ++w.itemRevision;
        }
        break;
    }
    case 0x42: {
        const auto target = key(r); r.finish();
        // CLEARCURSOR names the owning player, not the consumed item.
        if ((target.type == 0 && target.id == v.load.playerUnitId) || target.type == 6) {
            std::vector<uint32_t> consumed;
            for (const auto &[id, item] : w.items)
                if (onlineCursorItem(item, v.load.playerUnitId))
                    consumed.push_back(id);
            for (auto id : consumed) removeItem(w, id);
        }
        break;
    }
    case 0x97:
        r.finish(); w.weaponSet ^= 1; ++w.itemRevision;
        break;
    case 0x9C:
    case 0x9D:
        itemPacket(v, p, pd2);
        equipment(v, p);
        break;
    case 0xAB: {
        const auto target=key(r); const auto life=r.u8(); r.finish();
        // A health refresh neither creates a unit nor interrupts its path,
        // skill or hit animation. Removed units cannot be revived by a late AB.
        if(auto found=w.units.find(target);found!=w.units.end()) {
            found->second.lifePercent=life;
            found->second.lifeCarriesTriggerFlag=false;
        }
        break;
    }
    case 0xAC: {
        auto &u = unit(w, {1, r.u32()});
        u.classId = r.u16();
        u.equipmentObserved=true;
        position(v, u, point(r));
        u.lifePercent = r.u8(); u.lifeCarriesTriggerFlag = false;
        if (r.u8() != p.body.size() + 1)
            throw ProtocolError("Invalid NPC assignment size");
        const auto tail = r.take(r.remaining());
        u.appearanceBits.assign(tail.begin(), tail.end());
        u.assignmentRevision = w.revision;
        u.actionSkill.reset(); u.actionSkillLevel.reset(); u.direction.reset();
        u.actionRevision = w.revision; u.actionReceivedMilliseconds = receivedMilliseconds(); u.nativeMode = true;
        if (!tail.empty()) {
            BitReader bits(tail);
            u.mode = uint8_t(bits.read(4));
        }
        ++u.appearanceRevision;
        break;
    }
    default:
        // D2gsStream has already delimited this packet with the selected
        // profile's verified length. Missing world/UI semantics must not
        // disconnect a live game; keep diagnostics until a consumer exists.
        // Unknown framing and malformed handled messages still fail earlier.
        ++w.ignoredPackets;
        ++v.gameProtocol.unconsumed[p.id];
        return;
    }
    if (w.itemRevision != previousItems && w.playerTrade.phase == OnlinePlayerTrade::Phase::Open)
        w.playerTrade.revision = w.revision;
}
} // namespace d2x::net
