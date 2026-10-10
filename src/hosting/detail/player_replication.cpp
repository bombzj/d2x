#include "native_realm_service.hpp"
#include "hosting/native_game_wire.hpp"
#include "hosting/native_item_wire.hpp"
#include "gameplay/combat/life.hpp"
#include <cmath>
#include <algorithm>
namespace d2x::hosting {
void NativeRealmService::publishPlayers() {
    const auto participants = host.participants(binding->game);
    std::set<EntityId> present;
    std::vector<Bytes> packets;
    const auto local=host.read(*binding);
    std::set<EntityId> allies;
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
            peer.social.erase(view->actor.id);
        }
        const auto relation=local->socialRelations.find(id);
        const uint8_t status=id==binding->player?1:relation==local->socialRelations.end()?0:relation->second.partyStatus;
        const uint16_t flags=relation==local->socialRelations.end()?0:relation->second.flags;
        auto roster=encodeServerPacket(ServerMessage::PartyMember,[&](auto &out){
            out.u32(uint32_t(view->actor.id.value));out.u16(view->partyId);out.u16(uint16_t(view->level));out.u16(flags);out.u16(status);
        });
        if(!peer.social.contains(view->actor.id) || peer.social.at(view->actor.id)!=roster) {
            packets.push_back(roster);peer.social[view->actor.id]=std::move(roster);
            packets.push_back(encodeServerPacket(ServerMessage::PlayerPartyFlags,[&](auto &out){out.u32(uint32_t(view->actor.id.value));out.u16(view->partyId);}));
            if(id!=binding->player) packets.push_back(encodeServerPacket(ServerMessage::PlayerRelationship,[&](auto &out){out.u32(uint32_t(view->actor.id.value));out.u8(status);}));
        }
        if(id!=binding->player && local->partyId!=UINT16_MAX && local->partyId==view->partyId) {
            allies.insert(view->actor.id);
            auto vitals=encodeServerPacket(ServerMessage::AllyPosition,[&](auto &out){
                out.u8(1);out.u16(playerLifePercentage(int64_t(view->life*256),int64_t(view->attributes.maxLife)*256));
                out.u32(uint32_t(view->actor.id.value));out.u16(uint16_t(view->actor.region));
            });
            if(!peer.partyVitals.contains(view->actor.id) || peer.partyVitals.at(view->actor.id)!=vitals) {
                packets.push_back(vitals);peer.partyVitals[view->actor.id]=std::move(vitals);
            }
            const auto position=view->actor.position+shared.terrain.at(binding->game).at(view->actor.region).origin;
            // Far party coordinates never assign a spatial player or reveal rooms.
            if(view->actor.region!=local->actor.region || (view->actor.position-local->actor.position).length()>50) {
                auto location=encodeServerPacket(ServerMessage::PartyVitals,[&](auto &out){
                    out.u32(uint32_t(view->actor.id.value));out.u32(uint32_t(std::floor(position.x)));out.u32(uint32_t(std::floor(position.y)));
                });
                if(!peer.partyPositions.contains(view->actor.id) || peer.partyPositions.at(view->actor.id)!=location) {
                    packets.push_back(location);peer.partyPositions[view->actor.id]=std::move(location);
                }
            } else peer.partyPositions.erase(view->actor.id);
        }
    }
    std::erase_if(peer.social,[&](const auto &p){return !present.contains(p.first);});
    std::erase_if(peer.partyVitals,[&](const auto &p){return !allies.contains(p.first);});
    std::erase_if(peer.partyPositions,[&](const auto &p){return !allies.contains(p.first);});
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
        const auto hover=peer.hover.find(actor);
        if(view->hover && view->hover->recipients.contains(binding->player)) {
            if(hover==peer.hover.end() || hover->second!=view->hover->revision) {
                packets.push_back(encodeServerPacket(ServerMessage::Chat,[&](auto &out){
                    out.u8(5);out.u8(0);out.u8(0);out.u32(uint32_t(actor.value));out.u8(0);out.u8(0);out.string({});out.string(view->hover->text);
                }));
                peer.hover[actor]=view->hover->revision;
            }
        } else if(hover!=peer.hover.end()) {
            packets.push_back(encodeServerPacket(ServerMessage::Reserved76,[&](auto &out){out.u8(0);out.u32(uint32_t(actor.value));}));
            peer.hover.erase(hover);
        }
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
    std::erase_if(peer.hover, [&](const auto &entry) { return !stateActors.contains(entry.first); });
    if (!packets.empty()) sendGameBatch(std::move(packets));
}
}
