#include "native_realm_service.hpp"
#include "hosting/native_combat_wire.hpp"
namespace d2x::hosting {
void NativeRealmService::publishMonsters() {
    if (!binding || peer.phase != GamePhase::Entered) return;
    std::set<EntityId> visible;
    std::vector<Bytes> packets;
    for (const auto &monster : host.visibleMonsters(*binding)) {
        visible.insert(monster.id);
        const auto origin = shared.terrain.at(binding->game).at(monster.area).origin;
        auto [entry, fresh] = peer.monsters.try_emplace(monster.id);
        if (fresh) packets.push_back(nativeMonsterAssignment(monster, origin));
        const auto statePacket = [&](int state, bool enabled) { return encodeServerPacket(enabled ? ServerMessage::EnableState : ServerMessage::DisableState, [&](auto &out) { out.u8(1); out.u32(uint32_t(monster.id.value)); out.u8(uint8_t(state)); }); };
        for (const int state : entry->second.states) if (!monster.states.contains(state)) packets.push_back(statePacket(state, false));
        for (const int state : monster.states) if (!entry->second.states.contains(state)) packets.push_back(statePacket(state, true));
        entry->second.states = monster.states;
        auto motion = nativeMonsterMotion(monster, origin);
        if (motion.empty()) entry->second.motion.clear();
        else if (fresh || motion != entry->second.motion) { packets.push_back(motion); entry->second.motion = std::move(motion); }
    }
    for (auto it = peer.monsters.begin(); it != peer.monsters.end();) {
        if (!visible.contains(it->first)) {
            packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(1); out.u32(uint32_t(it->first.value)); }));
            it = peer.monsters.erase(it);
        } else ++it;
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
}
}
