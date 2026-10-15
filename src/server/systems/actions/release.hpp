#pragma once
#include "requests.hpp"
#include "server/runtime/contracts.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace d2x::server::effects {struct BladeShieldPulse;struct MindBlastPulse;struct CorpseExplosionPulse;}
namespace d2x::server::skills {struct AssassinKickExecution;struct DruidMeleeExecution;}
namespace d2x::server::actions {
struct SkillRelease {
    ActorContext actor;
    SkillCastSpec skill;
    MissileCollisionRule collision;
    PointTarget target;
    EntityId unit;
    uint64_t tick{};
    uint8_t unitType{1}; uint64_t pulses{}; bool manaPaid{};
    std::optional<WeaponDamage> weapon{};
    std::optional<WeaponDamage> offhand{};
    std::vector<int> weaponHits{};
    size_t nextWeaponHit{};
    int weaponSpeed{}, weaponFrames{}, weaponRollback{};
    EntityId shield{};
    bool charging{}; float chargeSpeed{};
    bool leaping{},landed{};
    bool whirlwind{};uint64_t nextWhirlHit{};int whirlPeriod{};size_t whirlHand{};
    std::optional<uint64_t> nativeMeleeOccurrence;
    bool cancelled{};
    bool nativeCallbackStarted{};
    bool nativeStartPrepared{};
    std::shared_ptr<effects::BladeShieldPulse> bladeShieldPulse;
    std::shared_ptr<effects::MindBlastPulse> mindBlastPulse;
    std::shared_ptr<skills::AssassinKickExecution> assassinKick;
    std::shared_ptr<skills::DruidMeleeExecution> druidMelee;
};
struct MonsterRelease {
    MonsterCastRequest request;
    MonsterAttackRule attack;
    RegionId area;
    uint64_t generation{}, due{}, random{};
    uint64_t interruption{};
    std::optional<std::pair<Vec,Vec>> launch;
    std::vector<int> releaseFrames{};
    size_t nextRelease{};
    uint64_t started{};
    std::vector<EntityId> effectTargets{};
    size_t effectTarget{};
    bool targetsCaptured{};
    bool hitReleased{};
    uint64_t infernoUntil{};
    std::shared_ptr<effects::CorpseExplosionPulse> corpseExplosion;
    bool nativeCallbackStarted{},cancelled{};
    std::shared_ptr<SkillRelease> nativeSkill;
};
}
