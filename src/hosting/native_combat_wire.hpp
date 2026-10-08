#pragma once
#include "server/runtime/events.hpp"
#include "network/protocol/wire.hpp"
namespace d2x {
Bytes nativeMonsterAssignment(const MonsterSnapshot &, Vec origin);
Bytes nativeMonsterMotion(const MonsterSnapshot &, Vec origin);
std::vector<Bytes> nativeAttack(const server::AttackFact &, Vec origin);
Bytes nativeReposition(const server::RepositionFact &, Vec origin);
std::vector<Bytes> nativeHit(const server::HitFact &, Vec origin);
}
