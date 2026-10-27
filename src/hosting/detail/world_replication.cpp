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
        // LEVEL_ChangeAct drops the client's old room interest before 05/03.
        // Clearing only our cache leaves the retail client holding old units
        // while its act and room storage are released.
        std::set<std::pair<uint8_t,EntityId>> removals;
        for(const auto &[player,visible]:peer.visible) {
            (void)player;removals.emplace(0,visible.actor);
            for(const auto &[item,equipment]:visible.equipment) { (void)equipment;removals.emplace(4,item); }
        }
        for(const auto &[id,monster]:peer.monsters) { (void)monster;removals.emplace(1,id); }
        for(const auto &[id,revision]:peer.objects) { (void)revision;removals.emplace(2,id); }
        for(const auto &[id,revision]:peer.portals) { (void)revision;removals.emplace(2,id); }
        for(const auto &[id,revision]:peer.groundItems) { (void)revision;removals.emplace(4,id); }
        for(const auto &[id,revision]:peer.corpses) { (void)revision;removals.emplace(0,id); }
        for(const auto &[corpse,equipment]:peer.corpseEquipment) {
            (void)corpse;for(const auto id:equipment) removals.emplace(4,id);
        }
        for(const auto id:peer.warps) removals.emplace(5,id);
        std::vector<Bytes> unload;
        for(const auto &[type,id]:removals) unload.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out) {
            out.u8(type);out.u32(uint32_t(id.value));
        }));
        for(const auto &room:peer.rooms) unload.push_back(encodeServerPacket(ServerMessage::HideRoom,[&](auto &out) {
            out.u16(uint16_t(room.x));out.u16(uint16_t(room.y));out.u8(uint8_t(room.area));
        }));
        if(!unload.empty()) sendGameBatch(std::move(unload));
        sendGame(encodeServerPacket(ServerMessage::UnloadAct, [](auto &) {}));
        sendGame(encodeServerPacket(ServerMessage::LoadAct, [&](auto &out) {
            out.u8(uint8_t(destination.request.act)); out.u32(destination.request.seed);
            out.u16(uint16_t(actTownLevels.at(size_t(destination.request.act)))); out.u32(host.objectSeed(binding->game).value());
        }));
        peer.loadPending=true;
        const auto environment=host.environment(binding->game,region);
        if(!environment) throw net::protocol::ProtocolError("Loaded act lacks its native environment");
        sendGame(encodeServerPacket(ServerMessage::Environment,[&](auto &out) {
            out.u32(uint32_t(environment->cycle));out.u32(uint32_t(environment->ticks));out.u8(environment->eclipse?1:0);
        }));
        peer.areas.clear(); peer.rooms.clear(); peer.warps.clear(); peer.visible.clear(); peer.monsters.clear(); peer.missiles.clear(); peer.pets.clear(); peer.petVitals.clear(); peer.groundItems.clear(); peer.corpses.clear(); peer.corpseEquipment.clear(); peer.objects.clear(); peer.portals.clear(); peer.npcQuestAlerts.clear(); peer.shopItems.clear(); peer.shopOwner = {}; peer.states.clear();
    }
    std::set<RegionId> desired{region};
    const auto rooms = host.visibleRooms(*binding);
    for (const auto &room : rooms) desired.insert(room.area);
    std::vector<Bytes> packets;
    // Add references first, then remove the old ones, like ChangeClientRoom.
    // A shared seam never temporarily loses both of its collision owners.
    const auto reveal=[&](const auto &room) {if(!peer.rooms.insert(room).second) return;packets.push_back(encodeServerPacket(ServerMessage::RevealRoom, [&](auto &out) {
            out.u16(uint16_t(room.x)); out.u16(uint16_t(room.y)); out.u8(uint8_t(room.area));
        }));};
    const auto view=host.read(*binding);
    if (const auto *current=view?destination.map->activation.room(view->actor.position+Vec{.5f,.5f}):nullptr) {
        const server::RoomView own{region,(int(destination.origin.x)+current->x)/5,
            (int(destination.origin.y)+current->y)/5};
        if (!peer.rooms.contains(own) && std::find(rooms.begin(),rooms.end(),own)!=rooms.end()) reveal(own);
    }
    for (const auto &room : rooms) if (!peer.rooms.contains(room)) reveal(room);
    std::set<EntityId> warps;
    for (const auto next : desired) {
        const auto *definition=host.areaMetadata(binding->game,next);
        if(!definition) throw std::logic_error("Visible native area is missing");
        auto exits=definition->exits;
        std::erase_if(exits, [&](const auto &exit) {
            if (!host.visiblePoint(*binding, next, exit.position)) return true;
            warps.insert(exit.id);
            return peer.warps.contains(exit.id);
        });
        for (auto &packet : nativeAreaWarps(areas.at(next), exits)) packets.push_back(std::move(packet));
    }
    for (const auto id : peer.warps) if (!warps.contains(id))
        packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(5); out.u32(uint32_t(id.value)); }));
    if (!packets.empty()) sendGameBatch(std::move(packets));
    peer.areas = std::move(desired);
    peer.rooms.insert(rooms.begin(), rooms.end()); peer.warps = std::move(warps);
    if (terrain.request.level != int(region)) terrain = destination;
    if(crossAct) {
        // LEVEL_ChangeAct marks the player TELEPORTED. The changing-act
        // update sends 15(flag=1) before 04, so the original client attaches
        // its retained player to the destination room before finishing load.
        // publishAreas can run before the TravelFact is drained.
        if(!view) throw net::protocol::ProtocolError("Loaded act lacks its native player");
        const auto position=view->actor.position+terrain.origin;
        sendGame(encodeServerPacket(ServerMessage::Reposition,[&](auto &out) {
            out.u8(0);out.u32(uint32_t(view->actor.id.value));
            out.u16(uint16_t(std::lround(position.x)));out.u16(uint16_t(std::lround(position.y)));out.u8(1);
        }));
    }
}
void NativeRealmService::publishLoadComplete() {
    if(!binding || peer.phase!=GamePhase::Entered || !peer.loadPending || leavePending || !host.clientLoadReady(*binding)) return;
    // Initial admission and CHANGING_ACT both wait for D2Common_10073 after
    // publishing rooms and the local player's assignment/reposition.
    sendGame(encodeServerPacket(ServerMessage::LoadComplete,[](auto &) {}));
    peer.loadPending=false;
}
void NativeRealmService::publishRoomRemovals() {
    const auto rooms = host.visibleRooms(*binding);
    const std::set<server::RoomView> desiredRooms(rooms.begin(), rooms.end());
    std::vector<Bytes> packets;
    for (const auto &room : peer.rooms) if (!desiredRooms.contains(room))
        packets.push_back(encodeServerPacket(ServerMessage::HideRoom, [&](auto &out) {
            out.u16(uint16_t(room.x)); out.u16(uint16_t(room.y)); out.u8(uint8_t(room.area));
        }));
    if (!packets.empty()) sendGameBatch(std::move(packets));
    peer.rooms = desiredRooms;
}
void NativeRealmService::changeArea(const server::TravelFact &fact) {
    if (fact.player != binding->player) throw std::logic_error("Travel recipient mismatch");
    const auto &areas=shared.terrain.at(binding->game);
    const bool crossAct=areas.at(fact.from).request.act!=areas.at(fact.to).request.act;
    publishAreas(fact.to);
    if (fact.revived) sendGame(nativePlayerAssignment(*content, *host.exportCharacter(*binding,false), *host.read(*binding), terrain.origin));
    const auto position = fact.position + terrain.origin;
    if (!fact.walking && !crossAct) sendGame(encodeServerPacket(ServerMessage::Reposition, [&](auto &out) {
        out.u8(0); out.u32(uint32_t(fact.actor.value));
        out.u16(uint16_t(std::lround(position.x))); out.u16(uint16_t(std::lround(position.y))); out.u8(1);
    }));
    peer.lastMotion.clear(); peer.sentRevision = 0;
}
}

namespace d2x::hosting {
void NativeRealmService::publishGroundItems(std::span<const EntityId> pendingDrops) {
    std::map<EntityId, uint64_t> next;
    std::vector<Bytes> packets;
    for (const auto &item : host.groundItems(*binding)) {
        next.emplace(item.id, item.revision);
        if (const auto old = peer.groundItems.find(item.id); (old == peer.groundItems.end() || old->second != item.revision) &&
            std::find(pendingDrops.begin(),pendingDrops.end(),item.id)==pendingDrops.end()) {
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
    const auto owners = host.corpseOwners(binding->game);
    // 8E is the game-wide ownership roster, independent of room visibility.
    for (const auto &[corpse, owner] : peer.corpseOwners) if (!owners.contains(corpse))
        packets.push_back(encodeServerPacket(ServerMessage::CorpseAssignment, [&](auto &out) {
            out.u8(0); out.u32(uint32_t(owner.value)); out.u32(uint32_t(corpse.value));
        }));
    for (const auto &[corpse, owner] : owners) if (!peer.corpseOwners.contains(corpse))
        packets.push_back(encodeServerPacket(ServerMessage::CorpseAssignment, [&](auto &out) {
            out.u8(1); out.u32(uint32_t(owner.value)); out.u32(uint32_t(corpse.value));
        }));
    std::map<EntityId, std::set<EntityId>> equipment;
    // A recovered GUID may already be projected as owned/private or public
    // equipment this cycle. Removing the former corpse copy must not erase it.
    std::optional<std::set<EntityId>> retained;
    const auto retainedItem=[&](EntityId item) {
        // Ownership is needed only for an actual former-corpse item removal.
        // Keep this view local to the publication, including recovery/transfer.
        if(!retained) {
            retained.emplace();
            if (const auto local = host.exportCharacter(*binding,false)) for (const auto &[id, value] : local->inventory.items) {
                const auto *at = std::get_if<ContainerLocation>(&value.location);
                if (at && local->inventory.containers.at(at->container).spec.kind != ContainerKind::Corpse) retained->insert(id);
            }
            for (const auto player : host.visiblePlayers(*binding)) if (const auto visible = host.publicEquipment({binding->game, player}))
                for (const auto &[id, value] : visible->inventory.items) { (void)value; retained->insert(id); }
        }
        return retained->contains(item);
    };
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
            packets.push_back(nativePlayerMotion(motion, origin));
            packets.push_back(encodeServerPacket(ServerMessage::PlayerCorpse, [&](auto &out) { out.u8(1); out.u32(uint32_t(corpse.owner.value)); out.u32(uint32_t(corpse.id.value)); }));
        }
        auto items = nativeCorpseEquipment(*content, view.equipment);
        for (auto &packet : items) packets.push_back(std::move(packet));
        if (old != peer.corpses.end()) packets.push_back(nativePlayerMotion(motion, origin));
    }
    for (const auto &[corpse, previous] : peer.corpseEquipment) for (const auto id : previous)
        if ((!equipment.contains(corpse) || !equipment.at(corpse).contains(id)) && !retainedItem(id))
            packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(4); out.u32(uint32_t(id.value)); }));
    for (const auto &[id, revision] : peer.corpses) if (!next.contains(id)) {
        (void)revision;
        packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(0); out.u32(uint32_t(id.value)); }));
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
    peer.corpses.swap(next); peer.corpseEquipment.swap(equipment); peer.corpseOwners = owners;
}
}

namespace d2x::hosting {
void NativeRealmService::publishObjects() {
    std::map<EntityId, std::pair<uint64_t,int>> next; std::vector<Bytes> packets;
    for (const auto &object : host.visibleObjects(*binding)) {
        next.emplace(object.id, std::pair{object.revision,object.mode});
        const auto old = peer.objects.find(object.id); if (old != peer.objects.end() && old->second.first == object.revision) continue;
        if(object.rule.operation==23 && old!=peer.objects.end()) {
            // ObjMode::sub_6FC752A0 sends3/TARGETABLE/native mode. A waypoint
            // keeps its initial target flag; ENDANIM writes ON silently.
            if(object.mode==1) packets.push_back(encodeServerPacket(ServerMessage::UnitMode,[&](auto &out) {
                const auto area=host.area(binding->game,object.area);
                out.u8(2);out.u32(uint32_t(object.id.value));out.u8(3);
                out.u8(object.rule.selectable[area->definition.town?2:0]?2:0);out.u32(object.mode);
            }));
            continue;
        }
        if(old!=peer.objects.end() && (object.rule.chest || object.rule.shrine)) {
            // Existing-unit changes belong to frozen ObjectFacts. Retain the
            // delivered baseline even if a partial frame exposes newer state.
            next.at(object.id)=old->second;
            continue;
        } else {
            const auto position = object.position + shared.terrain.at(binding->game).at(object.area).origin;
            packets.push_back(encodeServerPacket(ServerMessage::AssignObject, [&](auto &out) {
                out.u8(2); out.u32(uint32_t(object.id.value)); out.u16(uint16_t(object.definition)); out.u16(uint16_t(std::lround(position.x))); out.u16(uint16_t(std::lround(position.y)));
                out.u8(uint8_t(object.mode)); out.u8(object.rule.shrine ? uint8_t(object.rule.shrine->code) : object.rule.chest ? uint8_t((object.rule.chest->locked ? 128 : 0) | object.rule.chest->trap) : object.rule.initialization==11 || object.rule.initialization==12 ? object.portalDestination : object.rule.operation==22 ? uint8_t(object.uses) : uint8_t(object.rule.trap));
            }));
        }
    }
    for (const auto &[id, revision] : peer.objects) if (!next.contains(id)) {
        (void)revision;
        packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(2); out.u32(uint32_t(id.value)); }));
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
    peer.objects.swap(next);
}
}

namespace d2x::hosting {
void NativeRealmService::publishNpcs() {
    // Identity, motion, HP and state/stat-list packets use publishMonsters.
    // This capability projection only owns per-observer quest alerts.
    // All callers synchronize publishMonsters first in this publication.
    // Reuse that recipient's exact membership, not another deep actor snapshot.
    std::vector<Bytes> packets;
    for(const auto area:host.visibleAreas(*binding)) {
        const auto *view=host.areaMetadata(binding->game,area);if(!view) continue;
        for(const auto &npc:view->npcs) if(peer.monsters.contains(npc.id)) {
            if(const auto message=host.npcQuestAlert(*binding,npc.rule,area)) {
                if(!peer.npcQuestAlerts.contains(npc.id) || peer.npcQuestAlerts.at(npc.id)!=*message)
                    packets.push_back(encodeServerPacket(ServerMessage::QuestAlert,[&](auto &out){out.u8(1);out.u32(uint32_t(npc.id.value));}));
                peer.npcQuestAlerts.insert_or_assign(npc.id,*message);
            } else peer.npcQuestAlerts.erase(npc.id);
        }
    }
    if(!packets.empty()) sendGameBatch(std::move(packets));
    std::erase_if(peer.npcQuestAlerts,[&](const auto &entry){return !peer.monsters.contains(entry.first);});
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
    std::map<EntityId,uint64_t> next; std::vector<Bytes> packets;
    const auto objects=host.visibleObjects(*binding);
    for(const auto &portal:host.visiblePortals(*binding)) {
        bool changed=false;
        // 0x82 introduces both GUIDs, including the endpoint outside visibility.
        // Track those placeholders too so replacing/closing a pair removes both.
        next.emplace(portal.fieldId, 0);
        next.emplace(portal.townId, 0);
        for(const bool town:{false,true}) {
            const auto area=town?portal.town:portal.field;
            if (!host.visiblePoint(*binding, area, town ? portal.townPosition : portal.fieldPosition)) continue;
            const auto id=town?portal.townId:portal.fieldId; next.insert_or_assign(id,portal.revision);
            if(peer.portals.contains(id) && peer.portals.at(id)==portal.revision) continue;
            changed=true; const auto position=(town?portal.townPosition:portal.fieldPosition)+shared.terrain.at(binding->game).at(area).origin;
            if(std::none_of(objects.begin(),objects.end(),[&](const auto &object){return object.id==id;}))
                packets.push_back(encodeServerPacket(ServerMessage::AssignObject,[&](auto &out){out.u8(2);out.u32(uint32_t(id.value));out.u16(uint16_t(portal.rule.definition));out.u16(uint16_t(std::lround(position.x)));out.u16(uint16_t(std::lround(position.y)));out.u8(portal.isOpened(town)?2:1);out.u8(uint8_t(town?portal.field:portal.town));}));
            packets.push_back(encodeServerPacket(ServerMessage::PortalState,[&](auto &out){out.u8(0);out.u8(uint8_t(town?portal.field:portal.town));out.u32(uint32_t(id.value));}));
        }
        if(changed && !portal.shared) packets.push_back(encodeServerPacket(ServerMessage::PortalOwner,[&](auto &out){out.u32(uint32_t(portal.owner.value));for(size_t i=0;i<16;++i) out.u8(i<portal.name.size()?uint8_t(portal.name[i]):0);out.u32(uint32_t(portal.fieldId.value));out.u32(uint32_t(portal.townId.value));}));
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
