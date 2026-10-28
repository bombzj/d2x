#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "native_identity.hpp"
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
Bytes nativeMonsterAssignment(const ClassicData &,const MonsterSnapshot &, Vec origin);
Bytes nativeMonsterMotion(const MonsterSnapshot &, Vec origin);
std::vector<Bytes> nativeAttack(const server::AttackFact &, Vec origin);
Bytes nativeReposition(const server::RepositionFact &, Vec origin);
std::vector<Bytes> nativeHit(const server::HitFact &, Vec origin);
Bytes nativeMissile(const server::MissileFact &,Vec,const hosting::NativeIdentityView &);
Bytes nativeState(const ClassicData &,const server::StateFact &,const hosting::NativeIdentityView &);
Bytes nativeStateSnapshot(const ClassicData &,EntityId,uint8_t,const std::set<int> &,
    const std::map<int,std::vector<StatValue>> &,const hosting::NativeIdentityView &);
Bytes nativeMonsterAssignment(const ClassicData &,const MonsterSnapshot &,Vec,const hosting::NativeIdentityView &);
Bytes nativeMonsterMotion(const MonsterSnapshot &,Vec,const hosting::NativeIdentityView &);
std::vector<Bytes> nativeAttack(const server::AttackFact &,Vec,const hosting::NativeIdentityView &);
Bytes nativeReposition(const server::RepositionFact &,Vec,const hosting::NativeIdentityView &);
std::vector<Bytes> nativeHit(const server::HitFact &,Vec,const hosting::NativeIdentityView &);
}
