#pragma once
#include "gameplay/monsters/monster_spawn.hpp"
#include <array>
#include <optional>
#include <string>

namespace d2x {
struct ProjectileResource {
    int id = -1;
    std::string art;
    float lifetime = 0;
};
struct PoisonCloudSpec {
    int missileId = -1;
    int minimum = 0, maximum = 0, poisonFrames = 0; // 1/256 HP per tick.
    int lifetimeFrames = 0, size = 0;
    int puffId = -1;
    bool damageFromSkill = false;
};
struct PoisonCloudBurstSpec {
    PoisonCloudSpec cloud;
    int mainStep = 1, subStep = 0;
    float mainSpeed = 0, subSpeed = 0;
};
// A server child missile can detonate later than the parent collision. Only
// its selected element is passed to the area hit (native SrvHit01).
struct AreaMissileSpec {
    int missileId = -1, delayFrames = 0;
    float radius = 0;
    MonsterDamageType element = MonsterDamageType::Fire;
    int minimum = 0, maximum = 0; // Resolved skill contribution, 1/256 HP.
    bool addEquipmentElement = false;
};
// Independent impact components. Importers resolve native hit functions and
// skill formulas; the executor knows neither item codes nor skill identities.
struct MissileImpactSpec {
    float radius = 0;
    int visualId = -1;
    float visualDuration = 0;
    std::optional<PoisonCloudBurstSpec> cloudBurst = {};
    std::optional<AreaMissileSpec> areaMissile = {};
};
// MonsterDamageType order: physical, magic, fire, lightning, cold, poison.
// Poison is a rate in HP/s; the other channels are HP per impact.
struct MissileImpactDamage {
    std::array<float, 6> channels{};
    float coldDuration = 0, poisonDuration = 0;
    bool freeze = false;
};
} // namespace d2x
