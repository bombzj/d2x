#include "native_realm_service.hpp"
#include "hosting/native_game_wire.hpp"
#include "hosting/native_item_wire.hpp"
#include "hosting/native_character_wire.hpp"
#include "hosting/native_combat_wire.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/combat/life.hpp"
#include <cmath>
#include <algorithm>
namespace d2x::hosting {
void NativeRealmService::publishPlayers(bool publishPartyVitals,std::span<const EntityId> inventoryResets,
    std::span<const std::pair<EntityId,EntityId>> relationUpdates) {
    const auto participants = host.participants(binding->game);
    std::set<EntityId> present;
    std::vector<Bytes> packets;
    const auto local=host.read(*binding);
    if(!local) return;
    std::map<PlayerId,EntityId> actors;
    std::set<EntityId> allies;
    std::map<PlayerId,PlayerSnapshot> views;
    bool joiningParty=false,leavingParty=false,localMembershipChanged=false;
    for(const auto id:participants) {
        auto view=host.read({binding->game,id});
        if(!view || !view->entered) continue;
        const auto previous=peer.social.find(view->actor.id);
        if(previous!=peer.social.end() && previous->second.party!=view->partyId) {
            joiningParty|=view->partyId!=UINT16_MAX;
            leavingParty|=previous->second.party!=UINT16_MAX;
            localMembershipChanged|=id==binding->player;
        }
        views.emplace(id,std::move(*view));
    }
    for (const auto &[id,playerView] : views) {
        const auto *view=&playerView;
        present.insert(view->actor.id);
        actors.emplace(id,view->actor.id);
        const auto identity = std::pair<uint64_t, std::string>{uint64_t(view->level), view->name};
        const auto found = peer.roster.find(view->actor.id);
        if (found == peer.roster.end() || found->second != identity) {
            CharacterRecord record; record.id = view->actor.id; record.name = view->name;
            record.characterClass = view->characterClass; record.level = view->level;
            packets.push_back(nativePlayerRoster(*content, record));
            peer.roster[view->actor.id] = identity;
        }
        const auto relation=local->socialRelations.find(id);
        const uint8_t status=id==binding->player?1:relation==local->socialRelations.end()?0:relation->second.partyStatus;
        const uint16_t flags=relation==local->socialRelations.end()?0:relation->second.flags;
        const auto previous=peer.social.find(view->actor.id);
        const bool initial=previous==peer.social.end();
        const bool membershipChanged=!initial && previous->second.party!=view->partyId;
        // ToggleHostile broadcasts 8C without 75. The other relationship
        // callbacks (Ignore/Squelch/Loot) still update the roster.
        const bool rosterRelationsChanged=!initial && ((previous->second.flags^flags)&~8u)!=0;
        const bool rosterChanged=(id!=binding->player && (initial || previous->second.level!=view->level || rosterRelationsChanged)) ||
            (leavingParty && (localMembershipChanged || membershipChanged));
        // PlayerList's invitation state is 8B. Membership (8D) and the
        // assignment roster (75) have independent publication lifetimes.
        if(rosterChanged)
            packets.push_back(encodeServerPacket(ServerMessage::PartyMember,[&](auto &out){
                out.u32(uint32_t(view->actor.id.value));out.u16(view->partyId);out.u16(uint16_t(view->level));out.u16(flags);out.u16(status);
            }));
        // PartyScreen rebuilds 8D for living players when joining. Leave uses
        // roster updates, plus a self 8D for each removed/disbanded member.
        if(initial || joiningParty || (membershipChanged && id==binding->player))
            packets.push_back(encodeServerPacket(ServerMessage::PlayerPartyFlags,[&](auto &out){out.u32(uint32_t(view->actor.id.value));out.u16(view->partyId);}));
        if(id!=binding->player && (!leavingParty || !rosterChanged) && (initial || previous->second.status!=status))
            packets.push_back(encodeServerPacket(ServerMessage::PlayerRelationship,[&](auto &out){out.u32(uint32_t(view->actor.id.value));out.u8(status);}));
        peer.social[view->actor.id]={view->partyId,uint16_t(view->level),flags,status};
        if(id!=binding->player && local->partyId!=UINT16_MAX && local->partyId==view->partyId) {
            allies.insert(view->actor.id);
            if(!publishPartyVitals) continue;
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
    // PlayerList broadcasts directed flags, including relations between two
    // other players. Rebuild these after all roster identities on late join.
    for(const auto &[id,actor]:actors) {
        for(const auto &[other,relation]:views.at(id).socialRelations) {
            const auto target=actors.find(other);
            if(target==actors.end() || target->second==actor) continue;
            const auto key=std::pair{actor,target->second};
            // Reliable PlayerList updates retain their own order and repeated
            // refreshes. Do not synthesize the same change before its fact.
            if(std::find(relationUpdates.begin(),relationUpdates.end(),key)!=relationUpdates.end()) continue;
            const auto sent=peer.relations.find(key);
            if(sent!=peer.relations.end() && sent->second==relation.flags) continue;
            packets.push_back(encodeServerPacket(ServerMessage::PlayerRelationFlags,[&](auto &out){
                out.u32(uint32_t(actor.value));out.u32(uint32_t(target->second.value));out.u16(relation.flags);
            }));
            peer.relations[key]=relation.flags;
        }
    }
    std::erase_if(peer.relations,[&](const auto &p){return !present.contains(p.first.first) || !present.contains(p.first.second);});
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
            for (const auto &[item, removal] : it->second.equipment)
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
        const bool inventoryChanged = fresh || sent.inventoryRevision != view->inventoryRevision;
        const bool characterChanged = fresh || sent.characterRevision != view->characterRevision;
        if (inventoryChanged || characterChanged) {
            auto equipment = *host.publicEquipment(source);
            if (fresh) {
                sent.actor = view->actor.id;
                packets.push_back(nativePlayerAssignment(*content, equipment, *view, area.origin));
            }
            if (characterChanged) {
                auto attributes = nativePublicCharacterAttributes(*content, equipment.player);
                if (fresh || attributes != sent.attributes) {
                    sent.attributes = attributes;
                    for (auto &packet : attributes) packets.push_back(std::move(packet));
                }
                sent.characterRevision = view->characterRevision;
            }
            if (inventoryChanged && std::find(inventoryResets.begin(),inventoryResets.end(),view->actor.id)==inventoryResets.end()) {
                if (!fresh) for (const auto &[item, removal] : sent.equipment)
                    if (!equipment.inventory.items.contains(item) && !peer.groundItems.contains(item))
                        for (const auto &packet : removal) packets.push_back(packet);
                auto appearance = nativePublicEquipment(*content, equipment);
                for (auto &packet : appearance) packets.push_back(std::move(packet));
                sent.equipment.clear();
                for (const auto &[key, item] : equipment.inventory.items)
                    sent.equipment.emplace(key, nativePublicUnequip(*content, equipment, item));
                sent.inventoryRevision = view->inventoryRevision;
            }
            if (fresh) packets.push_back(nativePlayerInitialStates(*content, *view));
        }
        const auto targetVisible=[&] {
            if(!view->actor.target) return false;
            const auto target=*view->actor.target;
            if(target.type==0) return target.id==local->actor.id || peer.corpses.contains(target.id) || std::any_of(peer.visible.begin(),peer.visible.end(),[&](const auto &p){return p.second.actor==target.id;});
            if(target.type==1) return peer.monsters.contains(target.id);
            if(target.type==2) return peer.objects.contains(target.id) || peer.portals.contains(target.id);
            if(target.type==4) return peer.groundItems.contains(target.id);
            if(target.type==5) {const auto current=host.area(binding->game,view->actor.region);return current && std::any_of(current->definition.exits.begin(),current->definition.exits.end(),[&](const auto &exit){return exit.id==target.id;});}
            return false;
        }();
        auto motion = nativePlayerMotion(*view, area.origin,targetVisible);
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
        auto &previousStats=peer.stateStats[actor];
        const auto emit = [&](int state, bool enabled) {
            const auto values=view->stateStats.find(state);
            auto packet=nativeState(*content,server::StateFact{actor,0,view->actor.region,state,enabled,
                enabled && values!=view->stateStats.end()?values->second:std::vector<StatValue>{}});
            if(!packet.empty()) packets.push_back(std::move(packet));
        };
        for (const auto state : previous) if (!view->states.contains(state)) {emit(state, false);previousStats.erase(state);}
        for (const auto state : view->states) {
            const auto values=view->stateStats.find(state);
            const bool changed=values!=view->stateStats.end() && (!previousStats.contains(state) || previousStats.at(state)!=values->second);
            if(!previous.contains(state) || changed) emit(state,true);
            if(values!=view->stateStats.end()) previousStats[state]=values->second;
        }
        previous = view->states;
    }
    std::erase_if(peer.states, [&](const auto &entry) { return !stateActors.contains(entry.first); });
    std::erase_if(peer.stateStats, [&](const auto &entry) { return !stateActors.contains(entry.first); });
    std::erase_if(peer.hover, [&](const auto &entry) { return !stateActors.contains(entry.first); });
    if (!packets.empty()) sendGameBatch(std::move(packets));
}
}
