#pragma once
#include "system.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/cast_spec.hpp"

namespace d2x::server::skills {
struct System::Release {
    ActorContext actor;
    SkillCastSpec skill;
    MissileCollisionRule collision;
    PointTarget target;
    EntityId unit;
    uint64_t tick{};
    uint8_t unitType{1}; uint64_t pulses{}; bool manaPaid{};
    std::optional<WeaponDamage> weapon{};
    std::vector<int> weaponHits{};
    size_t nextWeaponHit{};
    int weaponSpeed{}, weaponFrames{}, weaponRollback{};
    EntityId shield{};
    bool charging{}; float chargeSpeed{};
};
struct System::MonsterRelease {
    CastRequest request;
    MonsterAttackRule attack;
    RegionId area;
    uint64_t generation{}, due{}, random{};
    uint64_t interruption{};
    std::optional<std::pair<Vec,Vec>> launch;
    std::vector<int> releaseFrames{};
    size_t nextRelease{};
    uint64_t started{};
};
struct System::Runtime {
    struct Pending { ActorContext actor; Request request; };
    std::map<PlayerId,Pending> pending;
    std::map<EntityId,Release> releases;
    std::map<EntityId,MonsterRelease> monsters;
};
}