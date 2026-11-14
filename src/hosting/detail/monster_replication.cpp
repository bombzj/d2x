#include "server/game_messages.hpp"
#include "server/runtime/facts/skills.hpp"
#include "server/runtime/facts/combat.hpp"
#include "server/runtime/facts/effects.hpp"
#include "native_realm_service.hpp"
#include "hosting/native_combat_wire.hpp"
#include "hosting/native_item_wire.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>
namespace d2x::hosting {
void NativeRealmService::publishMissiles() {
    if(!binding || peer.phase!=GamePhase::Entered) return;
    std::set<EntityId> visible;std::vector<Bytes> packets;
    const auto own=host.read(*binding);if(!own) return;
    for(const auto &missile:host.visibleMissiles(*binding)) {
        const bool ownerVisible=missile.ownerType==1?peer.monsters.contains(missile.owner):
            missile.owner==own->actor.id || std::any_of(peer.visible.begin(),peer.visible.end(),[&](const auto &entry){return entry.second.actor==missile.owner;});
        if(!ownerVisible) continue;
        visible.insert(missile.missile);
        if(!peer.missiles.contains(missile.missile)) packets.push_back(nativeMissile(missile,shared.terrain.at(binding->game).at(missile.area).origin));
    }
    if(!packets.empty()) sendGameBatch(std::move(packets));
    // 73 has no missile GUID; native clients expire their local instance.
    // Removing an interest entry must never invent a RemoveUnit(3) packet.
    peer.missiles.swap(visible);
}
void NativeRealmService::publishMonsters(const std::vector<EntityId> &pendingDeaths,const std::vector<EntityId> &pendingActions) {
    if (!binding || peer.phase != GamePhase::Entered) return;
    std::set<EntityId> visible;
    std::vector<Bytes> packets;
    std::vector<Bytes> petAssignments;
    std::map<EntityId,GamePeer::PetProjection> pets;
    std::map<EntityId,Bytes> petVitals;
    const auto recipient=host.read(*binding);
    for(const auto &pet:host.pets(*binding)) {
        // Initial PlayerPets roster follows load completion. Live hire and
        // resurrection broadcast 81, while removals still use 7A.
        if(pet.hireling && !peer.initialLoadComplete) continue;
        const auto owner=wireId(pet.owner), id=wireId(pet.id);
        auto removal=encodeServerPacket(ServerMessage::PetOwnership,[&](auto &out){
            out.u8(0);out.u8(pet.type);out.u16(pet.nativeClass);out.u32(owner);out.u32(id);
        });
        auto assignment=removal;
        assignment[1]=1;
        if(pet.hireling) {
            const auto &record=*pet.hireling;
            const auto name=content->hirelingNameIds.find(record.nameKey);
            if(name==content->hirelingNameIds.end() || name->second<0 || name->second>=UINT16_MAX)
                throw std::runtime_error("Missing original hireling name index");
            assignment=encodeServerPacket(ServerMessage::HirelingIdentity,[&](auto &out) {
                out.u8(pet.type);out.u16(pet.nativeClass);out.u32(owner);out.u32(id);
                out.u32(record.seed);out.u32(uint32_t(name->second));
            });
        }
        const auto prior=peer.pets.find(pet.id);
        const bool changed=prior==peer.pets.end() || prior->second.assignment!=assignment;
        if(changed) {
            if(prior!=peer.pets.end()) packets.push_back(prior->second.removal);
            petAssignments.push_back(assignment);
        }
        pets.emplace(pet.id,GamePeer::PetProjection{std::move(assignment),std::move(removal)});
        // SCmd::sub_6FC3E200 sends monster 0x7F only to its owner, independently
        // of visible-unit removal. Its scale is 0..100, not motion's 0..128.
        if(recipient && pet.owner==recipient->actor.id) {
            auto vitals=encodeServerPacket(ServerMessage::AllyPosition,[&](auto &out){
                out.u8(0);out.u16(pet.lifePercentage);out.u32(wireId(pet.id));out.u16(uint16_t(pet.area));
            });
            const auto previous=peer.petVitals.find(pet.id);
            if(changed || previous==peer.petVitals.end() || previous->second!=vitals)
                petAssignments.push_back(vitals);
            petVitals.emplace(pet.id,std::move(vitals));
        }
    }
    for(const auto &[id,pet]:peer.pets) if(!pets.contains(id)) {
        packets.push_back(pet.removal);
        if(peer.hireling.actor==id) peer.hireling.dead=true;
    }
    peer.pets=std::move(pets);
    peer.petVitals=std::move(petVitals);
    std::vector<MonsterSnapshot> transient;
    const std::vector<MonsterSnapshot> *monsters=&transient;
    if(retainMonsterProjection) {
        if(!monsterProjection || monsterProjection->recipient!=recipient)
            monsterProjection=MonsterProjection{recipient,host.visibleMonsters(*binding)};
        monsters=&monsterProjection->monsters;
    } else transient=host.visibleMonsters(*binding);
    // Replacement retires the old hireable before publishing the new identity.
    // A normal room exit or death can leave the same hireable registered.
    const auto previousHireling=peer.hireling.actor;
    if(previousHireling && peer.monsters.contains(previousHireling) &&
        std::ranges::any_of(*monsters,[&](const auto &unit){return unit.hireling && unit.id!=previousHireling;})) {
        packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out){out.u8(1);out.u32(wireId(previousHireling));}));
        peer.monsters.erase(previousHireling);
    }
    packets.insert(packets.end(),std::make_move_iterator(petAssignments.begin()),std::make_move_iterator(petAssignments.end()));
    for (const auto &monster : *monsters) {
        visible.insert(monster.id);
        const auto origin = shared.terrain.at(binding->game).at(monster.area).origin;
        auto [entry, fresh] = peer.monsters.try_emplace(monster.id);
        if(!fresh && (entry->second.nativeClass!=monster.nativeClass || entry->second.modifiers!=monster.modifiers || entry->second.rankFlags!=monster.rankFlags || entry->second.nameSeed!=monster.nameSeed)) {
            packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out){out.u8(1);out.u32(wireId(monster.id));}));
            entry->second={};fresh=true;
        }
        if (fresh) {
            packets.push_back(nativeMonsterAssignment(*content,monster, origin));
            packets.push_back(nativeStateSnapshot(*content,monster.id,1,monster.states,monster.stateStats));
        }
        if(!fresh && monster.teleportRevision!=entry->second.teleportRevision) {
            const auto position=monster.position+origin;
            packets.push_back(encodeServerPacket(ServerMessage::Reposition,[&](auto &out){
                out.u8(1);out.u32(wireId(monster.id));
                out.u16(uint16_t(std::lround(position.x)));out.u16(uint16_t(std::lround(position.y)));out.u8(1);
            }));
            entry->second.motion.clear();
        }
        entry->second.teleportRevision=monster.teleportRevision;
        const bool pendingDeath=std::find(pendingDeaths.begin(),pendingDeaths.end(),monster.id)!=pendingDeaths.end();
        // Death and resurrection synchronize life through their native modes,
        // not through a regeneration AB in front of that mode notification.
        if(fresh || !entry->second.life || pendingDeath || monster.rising || monster.mode==0 || monster.mode==12) entry->second.life=monster.life;
        else if(monster.hostile && (std::abs(int(monster.life)-int(*entry->second.life))>4 || (!monster.life && *entry->second.life))) {
            // MonsterMode stat regeneration uses >4 on the native 0..128
            // scale. SUnitMsg publishes AB only to hostile observers.
            packets.push_back(encodeServerPacket(ServerMessage::UnitLife,[&](auto &out) {
                out.u8(1);out.u32(wireId(monster.id));out.u8(monster.life);
            }));
            entry->second.life=monster.life;
        }
        entry->second.nativeClass=monster.nativeClass;entry->second.modifiers=monster.modifiers;entry->second.rankFlags=monster.rankFlags;entry->second.nameSeed=monster.nameSeed;
        if(monster.hireling) {
            if(peer.hireling.actor!=monster.id) {peer.hireling={};peer.hireling.actor=monster.id;}
            const auto &values=monster.hirelingAttributes;
            const auto &statIds=content->nativeStats->names;
            bool refreshStats=peer.hireling.stats.empty() || (peer.hireling.dead && monster.hirelingLife>0);
            peer.hireling.dead=monster.hirelingLife<=0;
            for(const auto name:{"level","maxhp"}) {
                const auto current=values.find(name);
                const auto previous=peer.hireling.stats.find(uint8_t(statIds.at(name)));
                if(current!=values.end() && previous!=peer.hireling.stats.end())
                    refreshStats|=previous->second!=uint32_t(current->second);
            }
            // MONSTERAI_SendMercStats queues stats in this order. Numeric
            // identities still come from the mounted ItemStatCost catalog.
            for(const std::string_view name:{"level","strength","dexterity","maxhp","hitpoints","armorclass","experience",
                "nextexp","mindamage","maxdamage","fireresist","lightresist","coldresist","poisonresist"}) {
                const auto value=values.find(name);
                if(value==values.end()) continue;
                if(name=="hitpoints" && !refreshStats) continue;
                const auto stat=statIds.find(name);
                if(stat==statIds.end() || stat->second>UINT8_MAX || value->second<INT32_MIN || value->second>UINT32_MAX)
                    throw std::runtime_error("Hireling stat exceeds wire capacity");
                const auto statId=uint8_t(stat->second);
                const auto encoded=uint32_t(value->second);
                const auto prior=peer.hireling.stats.find(statId);
                if(!refreshStats && prior!=peer.hireling.stats.end() && prior->second==encoded) continue;
                const auto message=encoded<UINT8_MAX?ServerMessage::HirelingAttributeByte:
                    encoded<UINT16_MAX?ServerMessage::HirelingAttributeWord:ServerMessage::HirelingAttributeDword;
                packets.push_back(encodeServerPacket(message,[&](auto &out){
                    out.u8(statId);out.u32(wireId(monster.id));
                    if(encoded<UINT8_MAX) out.u8(uint8_t(encoded));
                    else if(encoded<UINT16_MAX) out.u16(uint16_t(encoded));
                    else out.u32(encoded);
                }));
                peer.hireling.stats[statId]=encoded;
            }
        }
        const auto statePacket = [&](int state, bool enabled) {
            const auto stats=monster.stateStats.find(state);
            auto values=enabled && stats!=monster.stateStats.end()?stats->second:std::vector<StatValue>{};
            // A7 only enables a state; it does not clear an earlier GOOD stat.
            // Send the explicit zero via A8 when alignment returns to EVIL.
            if(enabled && values.empty() && state==content->states.at("alignment").definition.id) {
                const auto previous=entry->second.stateStats.find(state);
                if(previous!=entry->second.stateStats.end()) {
                    values=previous->second;
                    for(auto &stat:values) stat.value=0;
                }
            }
            return nativeState(*content,server::StateFact{monster.id,1,monster.area,state,enabled,std::move(values)});
        };
        if(!fresh) {
            const auto appendState=[&](int state,bool enabled) {
                auto packet=statePacket(state,enabled);if(!packet.empty()) packets.push_back(std::move(packet));
            };
            for (const int state : entry->second.states) if (!monster.states.contains(state)) appendState(state,false);
            for (const int state : monster.states) if (!entry->second.states.contains(state) || (monster.stateStats.contains(state) ? entry->second.stateStats[state]!=monster.stateStats.at(state) : !entry->second.stateStats[state].empty())) appendState(state,true);
        }
        entry->second.states = monster.states;
        entry->second.stateStats = monster.stateStats;
        if(monster.equipment) {
            std::map<EntityId,uint64_t> equipment;
            for(const auto &[id,item]:monster.equipment->inventory.items) equipment.emplace(id,item.revision);
            if(fresh || equipment!=entry->second.equipment) {
                if(!monster.hireling) for(const auto &[id,revision]:entry->second.equipment) {
                    (void)revision;
                    if(!equipment.contains(id)) packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out){out.u8(4);out.u32(wireId(id));}));
                }
                auto gear=nativeMonsterEquipment(*content,*monster.equipment,monster.id,!monster.hireling.has_value());
                packets.insert(packets.end(),std::make_move_iterator(gear.begin()),std::make_move_iterator(gear.end()));
                entry->second.equipment=std::move(equipment);
            }
        }
        if(fresh && monster.appearOverlay>=0) packets.push_back(encodeServerPacket(ServerMessage::Overlay,[&](auto &out){out.u8(1);out.u32(wireId(monster.id));out.u16(uint16_t(monster.appearOverlay));}));
        // Reliable hit facts already begin GH/BL. A snapshot must not restart
        // their animations, including when only the UMod life flag changes.
        // A newly interested observer still needs the current reaction mode.
        if(pendingDeath || (!fresh && (monster.mode==3 || monster.mode==6))) continue;
        if(std::find(pendingActions.begin(),pendingActions.end(),monster.id)!=pendingActions.end()) continue;
        if(monster.skillAction) {
            if(fresh || entry->second.skillAction!=monster.skillAction->action) {
                auto action=*monster.skillAction;
                if(action.target && !(action.targetType==2 && action.skill)) {
                    const bool known=action.targetType==0?(recipient && recipient->actor.id==action.target) || peer.corpses.contains(action.target) ||
                        std::any_of(peer.visible.begin(),peer.visible.end(),[&](const auto &player){return player.second.actor==action.target;}):
                        action.targetType==1?peer.monsters.contains(action.target):
                        action.targetType==2?peer.objects.contains(action.target) || peer.portals.contains(action.target):
                        action.targetType==4?peer.groundItems.contains(action.target):action.targetType==5 && peer.warps.contains(action.target);
                    if(!known) action.target={};
                }
                auto cast=nativeAttack(action,shared.terrain.at(binding->game).at(action.area).origin);
                packets.insert(packets.end(),std::make_move_iterator(cast.begin()),std::make_move_iterator(cast.end()));
                entry->second.skillAction=action.action;
            }
            entry->second.motion.clear();
            continue;
        }
        auto motion = [&] {
            if(monster.movementTarget) {
                const auto target=*monster.movementTarget;
                const bool known=target.type==0?(recipient && recipient->actor.id==target.id) || peer.corpses.contains(target.id) ||
                    std::any_of(peer.visible.begin(),peer.visible.end(),[&](const auto &player){return player.second.actor==target.id;}):
                    target.type==1?peer.monsters.contains(target.id):
                    target.type==2?peer.objects.contains(target.id) || peer.portals.contains(target.id):
                    target.type==4?peer.groundItems.contains(target.id):target.type==5 && peer.warps.contains(target.id);
                if(!known) {
                    auto projected=monster;projected.movementTarget.reset();
                    return nativeMonsterMotion(projected, origin);
                }
            }
            return nativeMonsterMotion(monster, origin);
        }();
        auto baseline=motion;
        // MonsterMsg sends a path start on DOUPDATE, not on every movement
        // step. 68 carries the current XY only to initialize that path.
        // Keep the actual position on the wire (also for new observers), but
        // exclude it from change detection. A newly accepted path still sends
        // even when its target, parameters and destination have not changed.
        if(!baseline.empty() && baseline.front()==uint8_t(ServerMessage::NpcMoveUnit))
            std::fill(baseline.begin()+6,baseline.begin()+10,uint8_t{});
        // NU carries life, but regeneration is not a new stop order. Life
        // updates retain their AB/0C/hireling-stat publication above.
        if(!baseline.empty() && baseline.front()==uint8_t(ServerMessage::NpcReposition))
            baseline.back()=0;
        if (motion.empty()) entry->second.motion.clear();
        else if (fresh || baseline != entry->second.motion || (monster.moving && monster.movementRevision!=entry->second.movementRevision)) {
            packets.push_back(std::move(motion));entry->second.motion=std::move(baseline);
            entry->second.movementRevision=monster.movementRevision;
        }
    }
    for (auto it = peer.monsters.begin(); it != peer.monsters.end();) {
        if (!visible.contains(it->first)) {
            packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit, [&](auto &out) { out.u8(1); out.u32(wireId(it->first)); }));
            it = peer.monsters.erase(it);
        } else ++it;
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
}
}
