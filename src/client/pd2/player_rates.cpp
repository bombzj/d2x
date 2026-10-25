#include "../remote_ui_clients_impl.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <array>

namespace d2x {
std::optional<int32_t> RemoteUiClients::pd2ObservedStat(OnlineUnitKey key,std::string_view name) const {
    const auto &o=*impl_;const auto &world=o.session.read().world;
    const auto unit=world.units.find(key);const auto snapshot=o.combat.states().find(key);
    if(key.type!=0 || unit==world.units.end() || !unit->second.classId || !unit->second.equipmentObserved ||
       !unit->second.stateSequence || snapshot==o.combat.states().end() || !snapshot->second.decoded ||
       snapshot->second.sequence!=unit->second.stateSequence || !o.data.nativeStats) return {};
    const auto &schema=*o.data.nativeStats;
    const auto stat=schema.names.find(name);if(stat==schema.names.end()) return {};
    const auto id=stat->second;
    int value=0;
    if(id<=UINT8_MAX && unit->second.attributes.contains(uint8_t(id))) value=unit->second.attributes.at(uint8_t(id));
    else if(id==67 || id==68 || id==69) value=100;
    else if(id!=93 && name!="item_leap_speed" && name!="extra_bonespears" && name!="extra_holybolts") return {};
    const auto level=unit->second.attributes.find(uint8_t(schema.names.at("level")));
    if(level==unit->second.attributes.end()) return {};
    const auto gear=o.knownEquipmentStat(name,level->second,key);if(!gear) return {};
    value=nativeAdd(value,nativeShiftLeft(*gear,schema.definitions.at(id).valueShift));
    for(const auto &[state,list]:snapshot->second.states) {
        (void)state;
        for(const auto &entry:list.stats) if(entry.id==id && !entry.parameter) value=nativeAdd(value,nativeNarrow(entry.value));
    }
    return value;
}
std::optional<Pd2PlayerRates> RemoteUiClients::pd2PlayerRates(OnlineUnitKey key) const {
    const auto &o=*impl_;const auto &world=o.session.read().world;
    const auto unit=world.units.find(key);if(unit==world.units.end() || !unit->second.classId) return {};
    // Retail Client 191E0 initializes 67/68 to 100 for every player.
    // PlrMsg 82830 sends BASE values before public equipment. FullStats then
    // includes that unit's posted item/state lists; stat93 is not a mandatory
    // standalone 20 packet. Do not substitute the controlled player's totals.
    const std::array<std::string_view,3> names{"velocitypercent","attackrate","item_fasterattackrate"};
    std::array<int,3> values{};
    for(size_t index=0;index<names.size();++index) {
        if(o.session.read().load.playerUnitId==key.id && key.type==0) {
            if(const auto value=o.stat(names[index])) values[index]=int(*value);
            else {
                const auto level=o.stat("level");if(!level) return {};
                const auto contribution=o.knownSelfEquipmentStat(names[index],int(*level));if(!contribution) return {};
                values[index]=nativeAdd(index<2?100:0,*contribution);
            }
        } else {
            const auto value=pd2ObservedStat(key,names[index]);if(!value) return {};values[index]=*value;
        }
    }
    std::array<std::optional<int>,2> weaponSpeed;
    for(const auto &[id,item]:world.items) {
        (void)id;
        if(item.ownerType!=key.type || item.owner!=key.id || item.mode!=1 || (item.flags&0x4100u) ||
           (item.body!=4 && item.body!=5)) continue;
        const auto *base=o.data.items.find(item.code);if(!base) return {};
        if(base->equipment.isType("weap")) weaponSpeed[size_t(item.body-4)]=base->base.speed.value_or(0);
    }
    int speed=weaponSpeed[0].value_or(weaponSpeed[1].value_or(0));
    const auto cls=size_t(*unit->second.classId);if(cls>=o.data.characters.size()) return {};
    if(weaponSpeed[0] && weaponSpeed[1] && (o.data.characters[cls].code=="bar" || o.data.characters[cls].code=="ass"))
        speed=nativeDivide(nativeAdd(*weaponSpeed[0],*weaponSpeed[1]),2);
    values[1]=nativeSubtract(values[1],speed);
    return Pd2PlayerRates{values[0],values[1],values[2]};
}
}
