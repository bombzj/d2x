#include "native_realm_service.hpp"
#include "hosting/native_game_wire.hpp"
#include "content/world/world_catalog.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::hosting {
using net::protocol::Writer;
void NativeRealmService::publishAreas(RegionId region) {
    const auto &areas = shared.terrain.at(binding->game);
    const auto &destination = areas.at(region);
    const auto crossAct = terrain.request.act != destination.request.act;
    if (crossAct) {
        sendGame(encodeServerPacket(ServerMessage::UnloadAct, [](auto &) {}));
        sendGame(encodeServerPacket(ServerMessage::LoadAct, [&](auto &out) {
            out.u8(uint8_t(destination.request.act)); out.u32(destination.request.seed);
            out.u16(uint16_t(actTownLevels.at(size_t(destination.request.act)))); out.u32(0);
        }));
        peer.areas.clear(); peer.visible.clear(); peer.monsters.clear();
    }
    std::set<RegionId> desired{region};
    const auto definition = host.area(binding->game, region);
    for (const auto &edge : definition->definition.boundaries) if (areas.contains(edge.destination)) desired.insert(edge.destination);
    std::vector<Bytes> packets;
    for (const auto old : peer.areas) if (!desired.contains(old)) {
        const auto &area = areas.at(old);
        for (const auto &[x, y] : area.rooms) packets.push_back(encodeServerPacket(ServerMessage::HideRoom, [&](auto &out) {
            out.u16(uint16_t(x)); out.u16(uint16_t(y)); out.u8(uint8_t(old));
        }));
        const auto state = host.area(binding->game, old);
        const auto remove = [&](uint8_t type, EntityId id) { packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(type); out.u32(uint32_t(id.value)); })); };
        for (const auto &object : state->definition.objects) remove(2, object.id);
        for (const auto &exit : state->definition.exits) remove(5, exit.id);
    }
    for (const auto next : desired) if (!peer.areas.contains(next)) {
        const auto &area = areas.at(next);
        for (const auto &[x, y] : area.rooms) packets.push_back(encodeServerPacket(ServerMessage::RevealRoom, [&](auto &out) {
            out.u16(uint16_t(x)); out.u16(uint16_t(y)); out.u8(uint8_t(next));
        }));
        auto units = nativeAreaUnits(area, host.area(binding->game, next)->definition);
        for (auto &packet : units) packets.push_back(std::move(packet));
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
    peer.areas = std::move(desired);
    if (terrain.request.level != int(region)) terrain = destination;
    if (crossAct) sendGame(encodeServerPacket(ServerMessage::LoadComplete, [](auto &) {}));
}
void NativeRealmService::changeArea(const server::TravelFact &fact) {
    if (fact.player != binding->player) throw std::logic_error("Travel recipient mismatch");
    publishAreas(fact.to);
    const auto position = fact.position + terrain.origin;
    if (!fact.walking) sendGame(encodeServerPacket(ServerMessage::Reposition, [&](auto &out) {
        out.u8(0); out.u32(uint32_t(fact.actor.value));
        out.u16(uint16_t(std::lround(position.x))); out.u16(uint16_t(std::lround(position.y))); out.u8(0);
    }));
    peer.lastMotion.clear(); peer.sentRevision = 0;
}
}
