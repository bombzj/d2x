#include "native_combat_wire.hpp"
#include "protocol/message_catalog.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace d2x {
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
        out.u8(monster.life); out.u8(14);
        // MONMODE:4, optional component variations:0, optional rank flags:0.
        out.u8(monster.mode);
    });
}
Bytes nativeMonsterMotion(const MonsterSnapshot &monster, Vec origin) {
    if (monster.mode == 0 || monster.mode == 12)
        return encodeServerPacket(ServerMessage::NpcModePoint, [&](auto &out) {
            out.u32(uint32_t(monster.id.value)); out.u8(monster.mode == 12 ? 9 : 8);
            point(out, monster.position + origin); out.u8(0); out.u8(0);
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
    if (fact.actorType == 1)
        return {encodeServerPacket(ServerMessage::NpcAction, [&](auto &out) {
            out.u32(uint32_t(fact.actor.value)); out.u8(10); key(out, fact.targetType, fact.target);
            out.u8(direction(fact.position, fact.destination)); point(out, fact.position + origin);
        })};
    if (fact.target)
        return {encodeServerPacket(ServerMessage::CastUnit, [&](auto &out) {
            key(out, 0, fact.actor); out.u16(fact.skill); out.u8(fact.rank); key(out, fact.targetType, fact.target); out.u16(0);
        })};
    return {encodeServerPacket(ServerMessage::CastPoint, [&](auto &out) {
        key(out, 0, fact.actor); out.u32(fact.skill); out.u8(fact.rank); point(out, fact.destination + origin); out.u16(0);
    })};
}
Bytes nativeReposition(const server::RepositionFact &fact, Vec origin) {
    return encodeServerPacket(ServerMessage::Reposition, [&](auto &out) {
        key(out, 0, fact.actor); point(out, fact.position + origin); out.u8(0);
    });
}
std::vector<Bytes> nativeHit(const server::HitFact &fact, Vec origin) {
    std::vector<Bytes> result{encodeServerPacket(ServerMessage::Hit, [&](auto &out) {
        key(out, fact.type, fact.target); out.u8(0); out.u8(0); out.u8(fact.life);
    })};
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
