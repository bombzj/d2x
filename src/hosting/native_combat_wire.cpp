#include "native_combat_wire.hpp"
#include "protocol/message_catalog.hpp"
#include "content/classic_data.hpp"
#include "network/protocol/bits.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <bit>
#include <algorithm>
namespace d2x {
Bytes nativeState(const ClassicData &data,const server::StateFact &fact) {
    using hosting::ServerMessage;using hosting::encodeServerPacket;
    if(fact.state<0 || fact.state>=255) throw std::runtime_error("Native state id exceeds packet capacity");
    if(!fact.enabled || fact.stats.empty()) return encodeServerPacket(fact.enabled?ServerMessage::EnableState:ServerMessage::DisableState,[&](auto &out){out.u8(fact.type);out.u32(uint32_t(fact.actor.value));out.u8(uint8_t(fact.state));});
    const auto &table=data.tables.at("itemstatcost");net::protocol::BitWriter bits;
    for(const auto &[id,value]:fact.stats) {
        size_t row=0;while(row<table.rows().size() && table.number(row,"ID")!=id) ++row;
        if(row==table.rows().size() || id<0 || id>=511) throw std::runtime_error("Unknown native state stat");
        const int width=table.number(row,"Send Bits").value_or(0),param=table.number(row,"Send Param Bits").value_or(0);
        const bool signedValue=table.number(row,"Signed").value_or(0)!=0;
        if(width<1 || width>32 || param<0 || param>32 || value<(signedValue?-(int64_t(1)<<(width-1)):0) || value>=(int64_t(1)<<(width-(signedValue?1:0)))) throw std::runtime_error("Native state stat exceeds Send Bits");
        bits.write(uint32_t(id),9);bits.write(0,unsigned(param));bits.write(uint32_t(uint64_t(value)&((uint64_t(1)<<width)-1)),unsigned(width));
    }
    bits.write(511,9);auto packed=bits.release();if(packed.size()>247) throw std::runtime_error("Native state stat packet exceeds byte length");
    return encodeServerPacket(ServerMessage::EnableStateStats,[&](auto &out){out.u8(fact.type);out.u32(uint32_t(fact.actor.value));out.u8(uint8_t(packed.size()+8));out.u8(uint8_t(fact.state));out.append(packed);});
}
namespace {
using net::protocol::Writer;
using hosting::ServerMessage;
using hosting::encodeServerPacket;
void point(Writer &out, Vec point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0 || point.x > 65535 || point.y > 65535)
        throw std::runtime_error("Combat point exceeds native coordinate capacity");
    out.u16(uint16_t(std::lround(point.x))); out.u16(uint16_t(std::lround(point.y)));
}
void key(Writer &out, uint8_t type, EntityId id) {
    if (!id || id.value > UINT32_MAX) throw std::runtime_error("Combat identity exceeds native capacity");
    out.u8(type); out.u32(uint32_t(id.value));
}
uint8_t direction(Vec from, Vec to) {
    const auto delta = to - from;
    return uint8_t(int(std::lround(7.5 + std::atan2(-delta.x, delta.y) * 32. / std::numbers::pi)) & 63);
}
}
Bytes nativeMonsterAssignment(const MonsterSnapshot &monster, Vec origin) {
    return encodeServerPacket(ServerMessage::AssignNpc, [&](auto &out) {
        out.u32(uint32_t(monster.id.value)); out.u16(uint16_t(monster.nativeClass)); point(out, monster.position + origin);
        out.u8(monster.life);
        net::protocol::BitWriter bits;
        // SCmd::sub_6FC3FC80 retains skill/death modes; motion is sent separately.
        bits.write(monster.mode==0 || monster.mode==8 || monster.mode==9 || monster.mode==12?monster.mode:1,4);
        const bool components=std::any_of(monster.components.begin(),monster.components.end(),[](auto n){return n!=0;});
        bits.write(components,1);
        if(components) for(size_t c=0;c<monster.components.size();++c)
            bits.write(monster.components[c],monster.componentCounts[c]>=3?std::bit_width(unsigned(monster.componentCounts[c]-1)):1);
        bits.write(!monster.modifiers.empty(),1);
        if(!monster.modifiers.empty()) {
            if(monster.modifiers.size()>9) throw std::runtime_error("Native NPC modifier capacity exceeded");
            bits.write(0,5); // Summons are neither champion nor unique ranks.
            for(auto modifier:monster.modifiers) {if(!modifier) throw std::runtime_error("Invalid native NPC modifier");bits.write(modifier,8);}
            bits.write(0,8);bits.write(0,16);bits.write(0,1); // Terminator, name seed, hireling owner.
        }
        bits.write(monster.storedOwner.has_value(),1);
        if(monster.storedOwner) {
            if(!*monster.storedOwner || monster.storedOwner->value>0x7fffffff) throw std::runtime_error("Native stored owner exceeds 31 bits");
            bits.write(uint32_t(monster.storedOwner->value),31);
        }
        bits.write(0,1); // No assignment stat list; state packets follow.
        auto tail=bits.release();out.u8(uint8_t(13+tail.size()));out.append(tail);
    });
}
Bytes nativeMonsterMotion(const MonsterSnapshot &monster, Vec origin) {
    if(monster.mode==3 || monster.mode==6)
        return encodeServerPacket(ServerMessage::NpcModePoint,[&](auto &out){out.u32(uint32_t(monster.id.value));out.u8(monster.mode==3?6:18);point(out,monster.position+origin);out.u8(0);out.u8(0);});
    if(monster.mode==8 || monster.mode==9)
        return encodeServerPacket(ServerMessage::NpcModePoint,[&](auto &out){out.u32(uint32_t(monster.id.value));out.u8(monster.mode==8?12:14);point(out,monster.position+origin);out.u8(0);out.u8(0);});
    if (monster.mode == 0 || monster.mode == 12)
        return encodeServerPacket(ServerMessage::NpcModePoint, [&](auto &out) {
            out.u32(uint32_t(monster.id.value)); out.u8(monster.mode == 12 ? 9 : 8);
            point(out, monster.position + origin); out.u8(0); out.u8(0);
        });
    if(monster.mode==13)
        return encodeServerPacket(ServerMessage::NpcMovePoint,[&](auto &out) {
            out.u32(uint32_t(monster.id.value));out.u8(20);point(out,monster.knockbackSource+origin);
            out.u8(3);out.u8(109);out.u8(11);out.u16(0);out.u8(monster.life);
        });
    if (monster.attacking) return {};
    if (monster.moving)
        return encodeServerPacket(ServerMessage::NpcMovePoint, [&](auto &out) {
            out.u32(uint32_t(monster.id.value)); out.u8(monster.running ? 23 : 1); point(out, monster.destination + origin);
            out.u8(1); out.u8(0); out.u8(0); out.u16(uint16_t(monster.velocityPercent)); out.u8(0);
        });
    return encodeServerPacket(ServerMessage::NpcReposition, [&](auto &out) {
        out.u32(uint32_t(monster.id.value)); point(out, monster.position + origin); out.u8(monster.life);
    });
}
std::vector<Bytes> nativeAttack(const server::AttackFact &fact, Vec origin) {
    if (fact.actorType == 1 && !fact.skill)
        return {encodeServerPacket(ServerMessage::NpcAction, [&](auto &out) {
            out.u32(uint32_t(fact.actor.value)); out.u8(fact.monsterMode == 9 ? 15 : fact.monsterMode == 8 ? 13 : fact.monsterMode == 5 ? 16 : 10); key(out, fact.targetType, fact.target);
            out.u8(direction(fact.position, fact.destination)); point(out, fact.position + origin);
        })};
    if (fact.target)
        return {encodeServerPacket(ServerMessage::CastUnit, [&](auto &out) {
            key(out, fact.actorType, fact.actor); out.u16(fact.skill); out.u8(fact.rank); key(out, fact.targetType, fact.target); out.u16(0);
        })};
    return {encodeServerPacket(ServerMessage::CastPoint, [&](auto &out) {
        key(out, fact.actorType, fact.actor); out.u32(fact.skill); out.u8(fact.rank); point(out, fact.destination + origin); out.u16(0);
    })};
}
Bytes nativeReposition(const server::RepositionFact &fact, Vec origin) {
    return encodeServerPacket(ServerMessage::Reposition, [&](auto &out) {
        key(out, 0, fact.actor); point(out, fact.position + origin); out.u8(0);
    });
}
std::vector<Bytes> nativeHit(const server::HitFact &fact, Vec origin) {
    std::vector<Bytes> result{encodeServerPacket(ServerMessage::Hit, [&](auto &out) {
        key(out, fact.type, fact.target); out.u8(0); out.u8(fact.hitClass); out.u8(fact.life);
    })};
    if(fact.type==1 && !fact.killed && fact.monsterMode)
        result.push_back(encodeServerPacket(ServerMessage::NpcModePoint,[&](auto &out){out.u32(uint32_t(fact.target.value));out.u8(fact.monsterMode==3?6:18);point(out,fact.position+origin);out.u8(0);out.u8(0);}));
    if (fact.killed) {
        if (fact.type == 1) result.push_back(encodeServerPacket(ServerMessage::NpcModePoint, [&](auto &out) {
            out.u32(uint32_t(fact.target.value)); out.u8(8); point(out, fact.position + origin); out.u8(0); out.u8(0);
        }));
        else result.push_back(encodeServerPacket(ServerMessage::UnitIdle, [&](auto &out) {
            key(out, 0, fact.target); out.u8(8); point(out, fact.position + origin); out.u8(0); out.u8(0);
        }));
    }
    return result;
}
}
