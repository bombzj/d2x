#include "native_realm_service.hpp"
#include "hosting/native_combat_wire.hpp"
#include "hosting/native_game_wire.hpp"
#include "hosting/native_item_wire.hpp"
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
        peer.areas.clear(); peer.visible.clear(); peer.monsters.clear(); peer.pets.clear(); peer.groundItems.clear(); peer.corpses.clear(); peer.corpseEquipment.clear(); peer.objects.clear(); peer.portals.clear(); peer.npcs.clear();peer.npcStates.clear(); peer.shopItems.clear(); peer.shopOwner = {}; peer.states.clear();
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
    if (fact.revived) sendGame(nativePlayerAssignment(*content, *host.exportCharacter(*binding), *host.read(*binding), terrain.origin));
    const auto position = fact.position + terrain.origin;
    if (!fact.walking) sendGame(encodeServerPacket(ServerMessage::Reposition, [&](auto &out) {
        out.u8(0); out.u32(uint32_t(fact.actor.value));
        out.u16(uint16_t(std::lround(position.x))); out.u16(uint16_t(std::lround(position.y))); out.u8(0);
    }));
    peer.lastMotion.clear(); peer.sentRevision = 0;
}
}

namespace d2x::hosting {
void NativeRealmService::publishGroundItems() {
    std::map<EntityId, uint64_t> next;
    std::vector<Bytes> packets;
    for (const auto &item : host.groundItems(*binding)) {
        next.emplace(item.id, item.revision);
        if (const auto old = peer.groundItems.find(item.id); old == peer.groundItems.end() || old->second != item.revision) {
            const auto area = std::get<GroundLocation>(item.location).region;
            packets.push_back(nativeGroundItem(*content, item, shared.terrain.at(binding->game).at(area).origin));
        }
    }
    for (const auto &[id, revision] : peer.groundItems) if (!next.contains(id)) {
        (void)revision; packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(4); out.u32(uint32_t(id.value)); }));
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
    peer.groundItems.swap(next);
}
}

namespace d2x::hosting {
void NativeRealmService::publishCorpses() {
    std::map<EntityId, uint64_t> next; std::vector<Bytes> packets;
    std::map<EntityId, std::set<EntityId>> equipment;
    // A recovered GUID may already be projected as owned/private or public
    // equipment this cycle. Removing the former corpse copy must not erase it.
    std::set<EntityId> retained;
    if (const auto local = host.exportCharacter(*binding)) for (const auto &[id, item] : local->inventory.items) {
        const auto *at = std::get_if<ContainerLocation>(&item.location);
        if (at && local->inventory.containers.at(at->container).spec.kind != ContainerKind::Corpse) retained.insert(id);
    }
    for (const auto player : host.visiblePlayers(*binding)) if (const auto visible = host.publicEquipment({binding->game, player}))
        for (const auto &[id, item] : visible->inventory.items) { (void)item; retained.insert(id); }
    for (const auto &view : host.visibleCorpses(*binding)) {
        const auto &corpse = view.corpse; next.emplace(corpse.id, view.revision);
        auto &current = equipment[corpse.id];
        for (const auto &[id, item] : view.equipment.inventory.items) { (void)item; current.insert(id); }
        const auto old = peer.corpses.find(corpse.id);
        if (old != peer.corpses.end() && old->second == view.revision) continue;
        PlayerSnapshot motion; motion.actor.id = corpse.id; motion.actor.position = corpse.position; motion.deadSettled = true; motion.attributes.maxLife = 1;
        const auto origin = shared.terrain.at(binding->game).at(corpse.region).origin;
        if (old == peer.corpses.end()) {
            packets.push_back(nativePlayerAssignment(*content, view.equipment, motion, origin));
            packets.push_back(encodeServerPacket(ServerMessage::CorpseAssignment, [&](auto &out) { out.u8(1); out.u32(uint32_t(corpse.owner.value)); out.u32(uint32_t(corpse.id.value)); }));
        }
        auto items = nativeCorpseEquipment(*content, view.equipment);
        for (auto &packet : items) packets.push_back(std::move(packet));
        packets.push_back(nativePlayerMotion(motion, origin));
    }
    for (const auto &[corpse, previous] : peer.corpseEquipment) for (const auto id : previous)
        if ((!equipment.contains(corpse) || !equipment.at(corpse).contains(id)) && !retained.contains(id))
            packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(4); out.u32(uint32_t(id.value)); }));
    for (const auto &[id, revision] : peer.corpses) if (!next.contains(id)) {
        (void)revision;
        packets.push_back(encodeServerPacket(ServerMessage::CorpseAssignment, [&](auto &out) { out.u8(0); out.u32(0); out.u32(uint32_t(id.value)); }));
        packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(0); out.u32(uint32_t(id.value)); }));
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
    peer.corpses.swap(next); peer.corpseEquipment.swap(equipment);
}
}

namespace d2x::hosting {
void NativeRealmService::publishObjects() {
    std::map<EntityId, uint64_t> next; std::vector<Bytes> packets;
    for (const auto &object : host.visibleObjects(*binding)) {
        next.emplace(object.id, object.revision);
        const auto old = peer.objects.find(object.id); if (old != peer.objects.end() && old->second == object.revision) continue;
        const auto position = object.position + shared.terrain.at(binding->game).at(object.area).origin;
        packets.push_back(encodeServerPacket(ServerMessage::AssignObject, [&](auto &out) {
            out.u8(2); out.u32(uint32_t(object.id.value)); out.u16(uint16_t(object.definition)); out.u16(uint16_t(std::lround(position.x))); out.u16(uint16_t(std::lround(position.y)));
            out.u8(uint8_t(object.mode)); out.u8(object.rule.shrine ? uint8_t(object.rule.shrine->code) : object.rule.chest ? uint8_t((object.rule.chest->locked ? 128 : 0) | object.rule.chest->trap) : 0);
        }));
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
    peer.objects.swap(next);
}
}

namespace d2x::hosting {
void NativeRealmService::publishNpcs() {
    std::set<EntityId> next; std::vector<Bytes> packets;
    for (const auto area : host.visibleAreas(*binding)) {
        const auto view=host.area(binding->game,area); if (!view) continue;
        for (const auto &npc : view->definition.npcs) {
            next.insert(npc.id);
            const bool fresh=!peer.npcs.contains(npc.id);
            if(fresh) {
            MonsterSnapshot snapshot; snapshot.id=npc.id; snapshot.area=area; snapshot.nativeClass=npc.rule.nativeClass; snapshot.position=npc.position; snapshot.life=128;
            packets.push_back(nativeMonsterAssignment(snapshot,shared.terrain.at(binding->game).at(area).origin));
            }
            const auto states=host.unitStates(binding->game,npc.id);auto &previous=peer.npcStates[npc.id];
            for(const int id:states) if(!previous.contains(id)) packets.push_back(encodeServerPacket(ServerMessage::EnableState,[&](auto &out){out.u8(1);out.u32(uint32_t(npc.id.value));out.u8(uint8_t(id));}));
            for(const int id:previous) if(!states.contains(id)) packets.push_back(encodeServerPacket(ServerMessage::DisableState,[&](auto &out){out.u8(1);out.u32(uint32_t(npc.id.value));out.u8(uint8_t(id));}));
            previous=states;
        }
    }
    for (const auto id : peer.npcs) if (!next.contains(id)) packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out){out.u8(1);out.u32(uint32_t(id.value));}));
    if (!packets.empty()) sendGameBatch(std::move(packets));
    peer.npcs.swap(next);
    std::erase_if(peer.npcStates,[&](const auto &entry){return !peer.npcs.contains(entry.first);});
}
void NativeRealmService::publishShop() {
    const auto stock=host.shop(*binding); std::set<EntityId> next; std::vector<Bytes> packets;
    if (stock) {
        auto projection=*stock;
        for (const auto &[id,item] : stock->inventory.items) { (void)item; next.insert(id); if (peer.shopItems.contains(id)) projection.inventory.items.erase(id); }
        packets=nativeShopItems(*content,projection); peer.shopOwner=stock->player.id;
    }
    for (const auto id : peer.shopItems) if (!next.contains(id)) packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out){out.u8(4);out.u32(uint32_t(id.value));}));
    if (!packets.empty()) sendGameBatch(std::move(packets));
    peer.shopItems.swap(next);
}
}

namespace d2x::hosting {
void NativeRealmService::publishPortals() {
    std::map<EntityId,uint64_t> next; std::vector<Bytes> packets; const auto areas=host.visibleAreas(*binding);
    for(const auto &portal:host.visiblePortals(*binding)) {
        bool changed=false;
        // 0x82 introduces both GUIDs, including the endpoint outside visibility.
        // Track those placeholders too so replacing/closing a pair removes both.
        next.emplace(portal.fieldId, 0);
        next.emplace(portal.townId, 0);
        for(const bool town:{false,true}) {
            const auto area=town?portal.town:portal.field; if(std::find(areas.begin(),areas.end(),area)==areas.end()) continue;
            const auto id=town?portal.townId:portal.fieldId; next.insert_or_assign(id,portal.revision);
            if(peer.portals.contains(id) && peer.portals.at(id)==portal.revision) continue;
            changed=true; const auto position=(town?portal.townPosition:portal.fieldPosition)+shared.terrain.at(binding->game).at(area).origin;
            packets.push_back(encodeServerPacket(ServerMessage::AssignObject,[&](auto &out){out.u8(2);out.u32(uint32_t(id.value));out.u16(uint16_t(portal.rule.definition));out.u16(uint16_t(std::lround(position.x)));out.u16(uint16_t(std::lround(position.y)));out.u8(portal.opened?2:1);out.u8(uint8_t(town?portal.field:portal.town));}));
            packets.push_back(encodeServerPacket(ServerMessage::PortalState,[&](auto &out){out.u8(0);out.u8(uint8_t(town?portal.field:portal.town));out.u32(uint32_t(id.value));}));
        }
        if(changed) packets.push_back(encodeServerPacket(ServerMessage::PortalOwner,[&](auto &out){out.u32(uint32_t(portal.owner.value));for(size_t i=0;i<16;++i) out.u8(i<portal.name.size()?uint8_t(portal.name[i]):0);out.u32(uint32_t(portal.fieldId.value));out.u32(uint32_t(portal.townId.value));}));
    }
    for(const auto &[id,revision]:peer.portals) if(!next.contains(id) || (!next.at(id) && revision)) { (void)revision; packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out){out.u8(2);out.u32(uint32_t(id.value));})); }
    if(!packets.empty()) sendGameBatch(std::move(packets));
    peer.portals.swap(next);
}
void NativeRealmService::publishItemSkills() {
    const auto player=host.read(*binding); if(!player) return;
    std::vector<Bytes> packets;
    for(const auto &[skill,quantity]:player->itemSkills) if(!peer.itemSkills.contains(skill) || peer.itemSkills.at(skill)!=quantity)
        packets.push_back(encodeServerPacket(ServerMessage::ItemSkillQuantity,[&](auto &out){out.u8(0);out.u8(0);out.u32(uint32_t(player->actor.id.value));out.u16(uint16_t(skill));out.u8(uint8_t(quantity));out.u8(0);out.u8(0);}));
    if(!packets.empty()) sendGameBatch(std::move(packets));
    peer.itemSkills=player->itemSkills;
}
}
