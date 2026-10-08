#include "native_realm_service.hpp"
#include "hosting/native_game_wire.hpp"
#include "hosting/native_item_wire.hpp"
#include <algorithm>
namespace d2x::hosting {
void NativeRealmService::publishPlayers() {
    const auto participants = host.participants(binding->game);
    std::set<EntityId> present;
    std::vector<Bytes> packets;
    for (const auto id : participants) {
        const auto view = host.read({binding->game, id});
        if (!view || !view->entered) continue;
        present.insert(view->actor.id);
        const auto identity = std::pair<uint64_t, std::string>{uint64_t(view->level), view->name};
        const auto found = peer.roster.find(view->actor.id);
        if (found == peer.roster.end() || found->second != identity) {
            CharacterRecord record; record.id = view->actor.id; record.name = view->name;
            record.characterClass = view->characterClass; record.level = view->level;
            packets.push_back(nativePlayerRoster(*content, record));
            peer.roster[view->actor.id] = identity;
        }
    }
    for (auto it = peer.roster.begin(); it != peer.roster.end();) {
        if (!present.contains(it->first)) {
            packets.push_back(encodeServerPacket(ServerMessage::PlayerLeft, [&](auto &out) { out.u32(uint32_t(it->first.value)); }));
            it = peer.roster.erase(it);
        } else ++it;
    }
    const auto visible = host.visiblePlayers(*binding);
    for (auto it = peer.visible.begin(); it != peer.visible.end();) {
        if (std::find(visible.begin(), visible.end(), it->first) == visible.end()) {
            // Removed players may no longer have a snapshot; roster is cached
            // by actor identity separately from instance-local participant IDs.
            const auto actor = it->second.actor;
            for (const auto item : it->second.equipment)
                packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(4); out.u32(uint32_t(item.value)); }));
            packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(0); out.u32(uint32_t(actor.value)); }));
            it = peer.visible.erase(it);
        } else ++it;
    }
    for (const auto id : visible) {
        const PlayerBinding source{binding->game, id};
        const auto view = host.read(source);
        auto [entry, fresh] = peer.visible.try_emplace(id);
        auto &sent = entry->second;
        const auto &area = shared.terrain.at(binding->game).at(view->actor.region);
        if (fresh || sent.inventoryRevision != view->inventoryRevision) {
            auto equipment = *host.publicEquipment(source);
            if (fresh) {
                sent.actor = view->actor.id;
                packets.push_back(nativePlayerAssignment(*content, equipment, *view, area.origin));
            } else for (const auto item : sent.equipment)
                packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(4); out.u32(uint32_t(item.value)); }));
            auto appearance = nativePublicEquipment(*content, equipment);
            for (auto &packet : appearance) packets.push_back(std::move(packet));
            sent.equipment.clear();
            for (const auto &[key, item] : equipment.inventory.items) { (void)item; sent.equipment.insert(key); }
            sent.inventoryRevision = view->inventoryRevision;
        }
        auto motion = nativePlayerMotion(*view, area.origin);
        if (motion.empty()) sent.motion.clear();
        else if (fresh || motion != sent.motion) { packets.push_back(motion); sent.motion = std::move(motion); }
    }
    auto statePlayers = visible; statePlayers.push_back(binding->player);
    std::set<EntityId> stateActors;
    for (const auto id : statePlayers) {
        const auto view = host.read({binding->game, id});
        if (!view || !view->entered) continue;
        const auto actor = view->actor.id; stateActors.insert(actor);
        auto &previous = peer.states[actor];
        const auto emit = [&](int state, bool enabled) {
            packets.push_back(encodeServerPacket(enabled ? ServerMessage::EnableState : ServerMessage::DisableState,
                [&](auto &out) { out.u8(0); out.u32(uint32_t(actor.value)); out.u8(uint8_t(state)); }));
        };
        for (const auto state : previous) if (!view->states.contains(state)) emit(state, false);
        for (const auto state : view->states) if (!previous.contains(state)) emit(state, true);
        previous = view->states;
    }
    std::erase_if(peer.states, [&](const auto &entry) { return !stateActors.contains(entry.first); });
    if (!packets.empty()) sendGameBatch(std::move(packets));
}
}
