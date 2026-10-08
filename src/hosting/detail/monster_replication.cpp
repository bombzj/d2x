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
        const auto statePacket = [&](int state, bool enabled) {
            const auto stats=monster.stateStats.find(state);
            return nativeState(*content,{monster.id,1,monster.area,state,enabled,enabled && stats!=monster.stateStats.end()?stats->second:std::vector<std::pair<int,int64_t>>{}});
        };
        for (const int state : entry->second.states) if (!monster.states.contains(state)) packets.push_back(statePacket(state, false));
        for (const int state : monster.states) if (!entry->second.states.contains(state) || (monster.stateStats.contains(state) ? entry->second.stateStats[state]!=monster.stateStats.at(state) : !entry->second.stateStats[state].empty())) packets.push_back(statePacket(state, true));
        entry->second.states = monster.states;
        entry->second.stateStats = monster.stateStats;
        if(fresh && monster.equipment) {
            auto gear=nativeMonsterEquipment(*content,*monster.equipment,monster.id);
            packets.insert(packets.end(),std::make_move_iterator(gear.begin()),std::make_move_iterator(gear.end()));
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
