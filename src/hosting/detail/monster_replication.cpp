#include "native_realm_service.hpp"
#include "hosting/native_combat_wire.hpp"
#include "hosting/native_item_wire.hpp"
namespace d2x::hosting {
void NativeRealmService::publishMonsters() {
    if (!binding || peer.phase != GamePhase::Entered) return;
    std::set<EntityId> visible;
    std::vector<Bytes> packets;
    std::map<EntityId,Bytes> pets;
    for(const auto &pet:host.pets(*binding)) {
        auto packet=encodeServerPacket(ServerMessage::PetOwnership,[&](auto &out){
            out.u8(1);out.u8(pet.type);out.u16(pet.nativeClass);out.u32(uint32_t(pet.owner.value));out.u32(uint32_t(pet.id.value));
        });
        const auto prior=peer.pets.find(pet.id);
        if(prior==peer.pets.end() || prior->second!=packet) {
            if(prior!=peer.pets.end()) {auto removal=prior->second;removal[1]=0;packets.push_back(std::move(removal));}
            packets.push_back(packet);
        }
        pets.emplace(pet.id,std::move(packet));
    }
    for(const auto &[id,packet]:peer.pets) if(!pets.contains(id)) {auto removal=packet;removal[1]=0;packets.push_back(std::move(removal));}
    peer.pets=std::move(pets);
    for (const auto &monster : host.visibleMonsters(*binding)) {
        visible.insert(monster.id);
        const auto origin = shared.terrain.at(binding->game).at(monster.area).origin;
        auto [entry, fresh] = peer.monsters.try_emplace(monster.id);
        if (fresh) packets.push_back(nativeMonsterAssignment(monster, origin));
        if(monster.hireling) {
            const auto &record=*monster.hireling;
            const auto definition=std::find_if(content->hirelings.begin(),content->hirelings.end(),[&](const auto &entry){return entry.sourceRow==record.sourceRow;});
            const auto name=content->hirelingNameIds.find(record.nameKey);
            if(definition==content->hirelings.end() || name==content->hirelingNameIds.end()) throw std::runtime_error("Missing prepared hireling identity");
            auto identity=encodeServerPacket(ServerMessage::HirelingIdentity,[&](auto &out){
                out.u8(7);out.u16(uint16_t(record.classId));out.u32(uint32_t(monster.hirelingOwner.value));
                out.u32(uint32_t(monster.id.value));out.u32(record.seed);out.u32(uint32_t(name->second));
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
                packets.push_back(encodeServerPacket(ServerMessage::HirelingAttributeDword,[&](auto &out){out.u8(uint8_t(*stat));out.u32(uint32_t(monster.id.value));out.u32(encoded);}));
                entry->second.hirelingStats[uint8_t(*stat)]=encoded;
            }
        }
        const auto statePacket = [&](int state, bool enabled) {
            const auto stats=monster.stateStats.find(state);
            return nativeState(*content,{monster.id,1,monster.area,state,enabled,enabled && stats!=monster.stateStats.end()?stats->second:std::vector<std::pair<int,int64_t>>{}});
        };
        for (const int state : entry->second.states) if (!monster.states.contains(state)) packets.push_back(statePacket(state, false));
        for (const int state : monster.states) if (!entry->second.states.contains(state) || (monster.stateStats.contains(state) ? entry->second.stateStats[state]!=monster.stateStats.at(state) : !entry->second.stateStats[state].empty())) packets.push_back(statePacket(state, true));
        entry->second.states = monster.states;
        entry->second.stateStats = monster.stateStats;
        if(monster.equipment) {
            std::map<EntityId,uint64_t> equipment;
            for(const auto &[id,item]:monster.equipment->inventory.items) equipment.emplace(id,item.revision);
            if(fresh || equipment!=entry->second.equipment) {
                if(!monster.hireling) for(const auto &[id,revision]:entry->second.equipment) {
                    (void)revision;
                    if(!equipment.contains(id)) packets.push_back(encodeServerPacket(ServerMessage::RemoveUnit,[&](auto &out){out.u8(4);out.u32(uint32_t(id.value));}));
                }
                auto gear=nativeMonsterEquipment(*content,*monster.equipment,monster.id,!monster.hireling.has_value());
                packets.insert(packets.end(),std::make_move_iterator(gear.begin()),std::make_move_iterator(gear.end()));
                entry->second.equipment=std::move(equipment);
            }
        }
        if(fresh && monster.appearOverlay>=0) packets.push_back(encodeServerPacket(ServerMessage::Overlay,[&](auto &out){out.u8(1);out.u32(uint32_t(monster.id.value));out.u16(uint16_t(monster.appearOverlay));}));
        // Reliable hit facts already begin GH/BL. A snapshot must not restart
        // their animations, including when only the UMod life flag changes.
        // A newly interested observer still needs the current reaction mode.
        if(!fresh && (monster.mode==3 || monster.mode==6)) continue;
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
