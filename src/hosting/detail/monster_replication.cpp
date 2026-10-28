#include "native_realm_service.hpp"
#include "hosting/native_combat_wire.hpp"
#include "hosting/native_item_wire.hpp"
#include <algorithm>
#include <cstdlib>
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
void NativeRealmService::publishMonsters(const std::vector<EntityId> &pendingDeaths) {
    if (!binding || peer.phase != GamePhase::Entered) return;
    std::set<EntityId> visible;
    std::vector<Bytes> packets;
    std::map<EntityId,Bytes> pets;
    std::map<EntityId,Bytes> petVitals;
    const auto recipient=host.read(*binding);
    for(const auto &pet:host.pets(*binding)) {
        auto packet=encodeServerPacket(ServerMessage::PetOwnership,[&](auto &out){
            out.u8(1);out.u8(pet.type);out.u16(pet.nativeClass);out.u32(wireId(pet.owner));out.u32(wireId(pet.id));
        });
        const auto prior=peer.pets.find(pet.id);
        if(prior==peer.pets.end() || prior->second!=packet) {
            if(prior!=peer.pets.end()) {auto removal=prior->second;removal[1]=0;packets.push_back(std::move(removal));}
            packets.push_back(packet);
        }
        pets.emplace(pet.id,std::move(packet));
        // SCmd::sub_6FC3E200 sends monster 0x7F only to its owner, independently
        // of visible-unit removal. Its scale is 0..100, not motion's 0..128.
        if(recipient && pet.owner==recipient->actor.id) {
            auto vitals=encodeServerPacket(ServerMessage::AllyPosition,[&](auto &out){
                out.u8(0);out.u16(pet.lifePercentage);out.u32(wireId(pet.id));out.u16(uint16_t(pet.area));
            });
            const auto previous=peer.petVitals.find(pet.id);
            if(prior==peer.pets.end() || prior->second!=pets.at(pet.id) || previous==peer.petVitals.end() || previous->second!=vitals)
                packets.push_back(vitals);
            petVitals.emplace(pet.id,std::move(vitals));
        }
    }
    for(const auto &[id,packet]:peer.pets) if(!pets.contains(id)) {auto removal=packet;removal[1]=0;packets.push_back(std::move(removal));}
    peer.pets=std::move(pets);
    peer.petVitals=std::move(petVitals);
    std::vector<MonsterSnapshot> transient;
    const std::vector<MonsterSnapshot> *monsters=&transient;
    if(retainMonsterProjection) {
        if(!monsterProjection || monsterProjection->recipient!=recipient)
            monsterProjection=MonsterProjection{recipient,host.visibleMonsters(*binding)};
        monsters=&monsterProjection->monsters;
    } else transient=host.visibleMonsters(*binding);
    for (const auto &monster : *monsters) {
        visible.insert(monster.id);
        const auto origin = shared.terrain.at(binding->game).at(monster.area).origin;
        auto [entry, fresh] = peer.monsters.try_emplace(monster.id);
        if(!fresh && (entry->second.nativeClass!=monster.nativeClass || entry->second.modifiers!=monster.modifiers || entry->second.rankFlags!=monster.rankFlags || entry->second.nameSeed!=monster.nameSeed)) {
            packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out){out.u8(1);out.u32(wireId(monster.id));}));
            entry->second={};fresh=true;
        }
        if (fresh) packets.push_back(nativeMonsterAssignment(*content,monster, origin));
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
            const auto &record=*monster.hireling;
            const auto definition=std::find_if(content->hirelings.begin(),content->hirelings.end(),[&](const auto &entry){return entry.sourceRow==record.sourceRow;});
            const auto name=content->hirelingNameIds.find(record.nameKey);
            if(definition==content->hirelings.end() || name==content->hirelingNameIds.end()) throw std::runtime_error("Missing prepared hireling identity");
            auto identity=encodeServerPacket(ServerMessage::HirelingIdentity,[&](auto &out){
                out.u8(7);out.u16(uint16_t(record.classId));out.u32(wireId(monster.hirelingOwner));
                out.u32(wireId(monster.id));out.u32(record.seed);out.u32(uint32_t(name->second));
            });
            if(identity!=entry->second.hirelingIdentity) {packets.push_back(identity);entry->second.hirelingIdentity=std::move(identity);}
            const auto &values=monster.hirelingAttributes;
            const auto &table=content->tables.at("itemstatcost");
            for(size_t row=0;row<table.rows().size();++row) {
                const auto value=values.find(table.value(row,"Stat"));if(value==values.end()) continue;
                const auto stat=table.number(row,"ID");if(!stat || *stat<0 || *stat>255 || value->second<INT32_MIN || value->second>UINT32_MAX) throw std::runtime_error("Hireling stat exceeds wire capacity");
                const auto encoded=uint32_t(value->second);
                const auto prior=entry->second.hirelingStats.find(uint8_t(*stat));
                if(prior!=entry->second.hirelingStats.end() && prior->second==encoded) continue;
                packets.push_back(encodeServerPacket(ServerMessage::HirelingAttributeDword,[&](auto &out){out.u8(uint8_t(*stat));out.u32(wireId(monster.id));out.u32(encoded);}));
                entry->second.hirelingStats[uint8_t(*stat)]=encoded;
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
        if(fresh) packets.push_back(nativeStateSnapshot(*content,monster.id,1,monster.states,monster.stateStats));
        else {
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
        auto motion = [&] {
            if(monster.movementTarget) {
                const auto target=*monster.movementTarget;
                const bool known=target.type==0?(recipient && recipient->actor.id==target.id) || std::any_of(peer.visible.begin(),peer.visible.end(),[&](const auto &p){return p.second.actor==target.id;}):peer.monsters.contains(target.id);
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
