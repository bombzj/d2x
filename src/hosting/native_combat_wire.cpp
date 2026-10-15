#include "native_combat_wire.hpp"
#include "protocol/message_catalog.hpp"
#include "content/classic_data.hpp"
#include "network/protocol/bits.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <bit>
#include <algorithm>
#include <optional>
namespace d2x {
Bytes nativeMissile(const server::MissileFact &missile,Vec origin) {
    return hosting::encodeServerPacket(hosting::ServerMessage::Missile,[&](auto &out) {
        out.u32(0);out.u16(uint16_t(missile.definition));
        out.u32(uint32_t(std::floor(missile.position.x+origin.x)));out.u32(uint32_t(std::floor(missile.position.y+origin.y)));
        const bool stationary=missile.destination.x==0 && missile.destination.y==0;
        out.u32(stationary?0:uint32_t(std::floor(missile.destination.x+origin.x)));
        out.u32(stationary?0:uint32_t(std::floor(missile.destination.y+origin.y)));
        out.u16(uint16_t(missile.frame));out.u8(missile.ownerType);out.u32(uint32_t(missile.owner.value));out.u8(uint8_t(missile.rank));out.u8(missile.pierce);
    });
}
namespace {
bool stateIsServerOnly(const ClassicData &data,int id) {
    for(const auto &[name,record]:data.states) {
        (void)name;if(record.definition.id==id) return record.definition.noSend;
    }
    throw std::runtime_error("Unknown native state identity");
}
void stateStats(net::protocol::BitWriter &bits,const ClassicData &data,const std::vector<StatValue> &stats) {
    const auto &table=data.tables.at("itemstatcost");
    auto ordered=stats;
    std::stable_sort(ordered.begin(),ordered.end(),[](const auto &first,const auto &second){return first.key<second.key;});
    // SUnitMsg copies sixteen base entries before filtering Send Bits.
    if(ordered.size()>16) ordered.resize(16);
    for(const auto &stat:ordered) {
        const auto id=stat.key.id;int64_t value=stat.value;
        size_t row=0;while(row<table.rows().size() && table.number(row,"ID")!=id) ++row;
        if(row==table.rows().size()) continue;
        if(id>=511) throw std::runtime_error("Native state stat exceeds nine-bit identity");
        const int width=table.number(row,"Send Bits").value_or(0),param=table.number(row,"Send Param Bits").value_or(0);
        if(!width) continue;
        const bool signedValue=table.number(row,"Signed").value_or(0)!=0;
        if(width<1 || width>32 || param<0 || param>32) throw std::runtime_error("Invalid native state stat widths");
        if(width<32) {
            if(signedValue) value=std::clamp(value,-(int64_t(1)<<(width-1)),(int64_t(1)<<(width-1))-1);
            else value=std::clamp(value,int64_t(0),(int64_t(1)<<width)-1);
        }
        bits.write(uint32_t(id),9);
        if(param) bits.write(uint32_t(stat.key.layer)&(param==32?UINT32_MAX:(uint32_t{1}<<param)-1),unsigned(param));
        const auto mask=width==32?UINT32_MAX:(uint32_t{1}<<width)-1;
        bits.write(uint32_t(value)&mask,unsigned(width));
    }
    bits.write(511,9);
}
}
Bytes nativeStateSnapshot(const ClassicData &data,EntityId actor,uint8_t type,const std::set<int> &states,
    const std::map<int,std::vector<StatValue>> &stats) {
    net::protocol::BitWriter bits;
    for(int id:states) {
        if(id<0 || id>=255) throw std::runtime_error("Native state id exceeds snapshot capacity");
        if(stateIsServerOnly(data,id)) continue;
        const auto list=stats.find(id);const bool present=list!=stats.end() && !list->second.empty();
        bits.write(uint32_t(id),8);bits.write(present,1);if(present) stateStats(bits,data,list->second);
    }
    bits.write(255,8);const auto packed=bits.release();
    if(packed.size()>244) throw std::runtime_error("Native state snapshot exceeds byte length");
    return hosting::encodeServerPacket(hosting::ServerMessage::StateSnapshot,[&](auto &out) {
        out.u8(type);out.u32(uint32_t(actor.value));out.u8(uint8_t(packed.size()+7));out.append(packed);
    });
}
Bytes nativeState(const ClassicData &data,const server::StateFact &fact) {
    using hosting::ServerMessage;using hosting::encodeServerPacket;
    if(fact.state<0 || fact.state>=255) throw std::runtime_error("Native state id exceeds packet capacity");
    if(stateIsServerOnly(data,fact.state)) return {};
    if(!fact.enabled || fact.stats.empty()) return encodeServerPacket(fact.enabled?ServerMessage::EnableState:ServerMessage::DisableState,[&](auto &out){out.u8(fact.type);out.u32(uint32_t(fact.actor.value));out.u8(uint8_t(fact.state));});
    net::protocol::BitWriter bits;stateStats(bits,data,fact.stats);
    auto packed=bits.release();if(packed.size()>247) throw std::runtime_error("Native state stat packet exceeds byte length");
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
Bytes monsterBlock(EntityId actor, const std::optional<MotionTarget> &target) {
    // Retail 1.13c action 18 (RVA 4E253) consumes a unit type/GUID, never
    // XY coordinates. A missing/invisible target uses a valid lookup type and
    // the native absent GUID (-1); PATH_SetTargetUnit accepts the null result.
    return encodeServerPacket(ServerMessage::NpcModeUnit, [&](auto &out) {
        out.u32(uint32_t(actor.value)); out.u8(18);
        if (target) key(out, target->type, target->id);
        else { out.u8(0); out.u32(UINT32_MAX); }
        out.u8(0);
    });
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
        const bool extended=!monster.modifiers.empty() || monster.rankFlags || bool(monster.hirelingOwner);
        bits.write(extended,1);
        if(extended) {
            if(monster.modifiers.size()>9) throw std::runtime_error("Native NPC modifier capacity exceeded");
            bits.write(monster.rankFlags,5);
            if(monster.rankFlags&4) bits.write(monster.superUniqueIndex,16);
            for(auto modifier:monster.modifiers) {if(!modifier) throw std::runtime_error("Invalid native NPC modifier");bits.write(modifier,8);}
            bits.write(0,8);bits.write(monster.nameSeed,16);
            bits.write(bool(monster.hirelingOwner),1);
            if(monster.hirelingOwner) {
                if(monster.hirelingOwner.value>UINT32_MAX) throw std::runtime_error("Native hireling owner exceeds 32 bits");
                bits.write(uint32_t(monster.hirelingOwner.value),32);
            }
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
    if(monster.mode==6) return monsterBlock(monster.id,monster.movementTarget);
    if(monster.mode==3)
        return encodeServerPacket(ServerMessage::NpcModePoint,[&](auto &out){out.u32(uint32_t(monster.id.value));out.u8(6);point(out,monster.position+origin);out.u8(uint8_t(monster.life>1?monster.life-1:monster.life)|(monster.lightningReady?0x80:0));out.u8(0);});
    if(monster.mode==8 && monster.moving)
        return encodeServerPacket(ServerMessage::NpcMovePoint,[&](auto &out){out.u32(uint32_t(monster.id.value));out.u8(12);point(out,monster.destination+origin);out.u8(8);out.u8(0);out.u8(0);out.u16(uint16_t(monster.velocityPercent));out.u8(0);});
    if(monster.mode>=8 && monster.mode<=11)
        return encodeServerPacket(monster.movementTarget?ServerMessage::NpcAction:ServerMessage::NpcActionPoint,[&](auto &out) {
            out.u32(uint32_t(monster.id.value));
            out.u8(monster.mode==8?(monster.movementTarget?13:12):monster.mode==9?(monster.movementTarget?15:14):monster.mode==10?(monster.movementTarget?27:26):(monster.movementTarget?29:28));
            if(monster.movementTarget) key(out,monster.movementTarget->type,monster.movementTarget->id);
            else point(out,monster.destination+origin);
            out.u8(0);if(!monster.movementTarget) out.u8(0);point(out,monster.position+origin);
        });
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
    if (monster.attacking) {
        if(!monster.moving) return {};
        return encodeServerPacket(ServerMessage::UnitPositions,[&](auto &out){out.u16(13);out.u8(1);key(out,1,monster.id);point(out,monster.position+origin);});
    }
    if(monster.moving && monster.movementTarget)
        return encodeServerPacket(ServerMessage::NpcMoveUnit,[&](auto &out) {
            out.u32(uint32_t(monster.id.value));out.u8(monster.running?24:0);point(out,monster.position+origin);
            key(out,monster.movementTarget->type,monster.movementTarget->id);
            out.u8(monster.pathSteps);out.u8(0);out.u8(monster.pathType);out.u16(uint16_t(monster.velocityPercent));out.u8(monster.pathDistance);
        });
    if (monster.moving)
        return encodeServerPacket(ServerMessage::NpcMovePoint, [&](auto &out) {
            out.u32(uint32_t(monster.id.value)); out.u8(monster.running ? 23 : 1); point(out, monster.destination + origin);
            out.u8(monster.pathSteps); out.u8(0); out.u8(monster.pathType); out.u16(uint16_t(monster.velocityPercent)); out.u8(monster.pathDistance);
        });
    return encodeServerPacket(ServerMessage::NpcReposition, [&](auto &out) {
        out.u32(uint32_t(monster.id.value)); point(out, monster.position + origin); out.u8(monster.life);
    });
}
std::vector<Bytes> nativeAttack(const server::AttackFact &fact, Vec origin) {
    if(fact.actorType==1 && !fact.skill && !fact.target)
        return {encodeServerPacket(ServerMessage::NpcActionPoint,[&](auto &out) {
            out.u32(uint32_t(fact.actor.value));out.u8(fact.monsterMode==9?14:fact.monsterMode==8?12:fact.monsterMode==5?17:11);
            point(out,fact.destination+origin);out.u8(direction(fact.position,fact.destination));out.u8(0);point(out,fact.position+origin);
        })};
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
    std::vector<Bytes> result;
    if(fact.type==0) {
        // PlrMsg::sub_6FC81C00: correction/life update is wire action 19,
        // not the monster-only 0C update or an invented player GH action.
        result.push_back(encodeServerPacket(ServerMessage::UnitIdle,[&](auto &out) {
            key(out,0,fact.target);out.u8(19);point(out,fact.position+origin);out.u8(fact.hitClass);out.u8(fact.life);
        }));
    } else if(!fact.killed && !fact.monsterMode) {
        // MonsterMsg::sub_6FC659E0 uses flag 19 for life/last hit class.
        // SUnitDmg does not set that flag when it enters GH/BL/death. Sending
        // both GH and 0C would run the client's hit callback twice.
        result.push_back(encodeServerPacket(ServerMessage::Hit,[&](auto &out) {
            key(out,fact.type,fact.target);out.u8(19);out.u8(fact.hitClass);out.u8(uint8_t(fact.life>1?fact.life-1:fact.life)|(fact.lightningReady?0x80:0));
        }));
    }
    if(fact.type==0 && !fact.killed && fact.knockback) result.push_back(encodeServerPacket(ServerMessage::MovePoint,[&](auto &out) {
        key(out,0,fact.target);out.u8(20);point(out,*fact.knockback+origin);out.u8(fact.hitClass);point(out,fact.position+origin);
    }));
    if(fact.type==0 && !fact.killed && !fact.knockback && fact.playerMode)
        result.push_back(encodeServerPacket(ServerMessage::UnitIdle,[&](auto &out) {
            key(out,0,fact.target);out.u8(fact.playerMode);point(out,fact.position+origin);out.u8(fact.hitClass);out.u8(fact.life);
        }));
    if(fact.type==1 && !fact.killed && fact.monsterMode==6)
        result.push_back(monsterBlock(fact.target,fact.actionTarget));
    else if(fact.type==1 && !fact.killed && fact.monsterMode)
        result.push_back(encodeServerPacket(ServerMessage::NpcModePoint,[&](auto &out){
            out.u32(uint32_t(fact.target.value));out.u8(fact.monsterMode==3?6:18);point(out,fact.position+origin);
            // GH overloads the direction byte with HP and the UMod trigger.
            out.u8(fact.monsterMode==3?uint8_t(fact.life>1?fact.life-1:fact.life)|(fact.lightningReady?0x80:0):0);out.u8(fact.hitClass);
        }));
    if (fact.killed) {
        if (fact.type == 1) result.push_back(encodeServerPacket(ServerMessage::NpcModePoint, [&](auto &out) {
            out.u32(uint32_t(fact.target.value)); out.u8(8); point(out, fact.position + origin); out.u8(0); out.u8(fact.hitClass);
        }));
        else result.push_back(encodeServerPacket(ServerMessage::UnitIdle, [&](auto &out) {
            key(out, 0, fact.target); out.u8(8); point(out, fact.position + origin); out.u8(0); out.u8(0);
        }));
    }
    return result;
}
}
