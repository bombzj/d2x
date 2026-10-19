#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "network/protocol/wire.hpp"
#include <map>
#include <set>
#include <vector>
namespace d2x {
struct ClassicData;
struct MonsterSnapshot;
struct StatValue;
namespace server {
struct MissileFact;
struct StateFact;
struct AttackFact;
struct RepositionFact;
struct HitFact;
}
Bytes nativeMissile(const server::MissileFact &,Vec origin);
Bytes nativeState(const ClassicData &,const server::StateFact &);
Bytes nativeStateSnapshot(const ClassicData &,EntityId,uint8_t,const std::set<int> &,
    const std::map<int,std::vector<StatValue>> &);
Bytes nativeMonsterAssignment(const MonsterSnapshot &, Vec origin);
Bytes nativeMonsterMotion(const MonsterSnapshot &, Vec origin);
std::vector<Bytes> nativeAttack(const server::AttackFact &, Vec origin);
Bytes nativeReposition(const server::RepositionFact &, Vec origin);
std::vector<Bytes> nativeHit(const server::HitFact &, Vec origin);
}
