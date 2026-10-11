#pragma once
#include "server/runtime/events.hpp"
#include "network/protocol/wire.hpp"
namespace d2x {
struct ClassicData;
Bytes nativeState(const ClassicData &,const server::StateFact &);
Bytes nativeStateSnapshot(const ClassicData &,EntityId,uint8_t,const std::set<int> &,
    const std::map<int,std::vector<std::pair<int,int64_t>>> &);
Bytes nativeMonsterAssignment(const MonsterSnapshot &, Vec origin);
Bytes nativeMonsterMotion(const MonsterSnapshot &, Vec origin);
std::vector<Bytes> nativeAttack(const server::AttackFact &, Vec origin);
Bytes nativeReposition(const server::RepositionFact &, Vec origin);
std::vector<Bytes> nativeHit(const server::HitFact &, Vec origin);
}
