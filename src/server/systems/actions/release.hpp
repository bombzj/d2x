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

namespace d2x::server::effects {struct BladeShieldPulse;struct MindBlastPulse;struct CorpseExplosionPulse;struct NativeAreaPulse;struct MonsterCursePulse;}
namespace d2x::server::effects {struct Pd2DarkPactPulse;}
namespace d2x::server::skills {struct AssassinKickExecution;struct DruidMeleeExecution;struct WeaponMeleeExecution;struct Pd2CastExecution;}
namespace d2x::server::combat {struct NativeSkillContact;}
namespace d2x::server::monsters {struct SkillChildrenExecution;}
namespace d2x::server::actions {
struct MonsterTentacleExecution {
    int count{},next{},nativeClass{-1};
    uint8_t nativeMode{1};
    Vec center;
    std::optional<Vec> point;
};
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
    bool whirlwind{};uint64_t nextWhirlHit{};size_t whirlHand{};
    size_t whirlwindAttackHand{};
    std::optional<uint64_t> motionFrame{};
    bool motionFinished{};
    std::optional<uint64_t> nativeMeleeOccurrence{};
    std::shared_ptr<combat::NativeSkillContact> nativeContact{};
    bool frenzyPrepared{};
    bool cancelled{};
    bool nativeCallbackStarted{};
    bool nativeStartPrepared{};
    bool eventDriven{};
    // SrvDo012/013 replan after a completed callback, from a game-frame anchor.
    int rewindRemaining{};
    int64_t animationAnchor{};
    std::vector<uint8_t> animationEvents{};
    std::shared_ptr<effects::BladeShieldPulse> bladeShieldPulse{};
    std::shared_ptr<effects::MindBlastPulse> mindBlastPulse{};
    std::shared_ptr<effects::CorpseExplosionPulse> corpseExplosion{};
    std::shared_ptr<effects::NativeAreaPulse> directSkill{};
    std::shared_ptr<effects::Pd2DarkPactPulse> pd2DarkPact{};
    std::shared_ptr<effects::MonsterCursePulse> monsterCurse{};
    std::shared_ptr<skills::AssassinKickExecution> assassinKick{};
    std::shared_ptr<skills::DruidMeleeExecution> druidMelee{};
    std::shared_ptr<skills::WeaponMeleeExecution> weaponMelee{};
    std::shared_ptr<skills::Pd2CastExecution> pd2Execution{};
};
struct MonsterRelease {
    MonsterCastRequest request;
    MonsterAttackRule attack;
    RegionId area;
    uint64_t generation{}, due{};
    uint64_t interruption{};
    std::optional<std::pair<Vec,Vec>> launch;
    std::vector<int> releaseFrames{};
    size_t nextRelease{};
    uint64_t started{};
    std::shared_ptr<effects::MonsterCursePulse> curse{};
    std::shared_ptr<monsters::SkillChildrenExecution> children{};
    bool formulaPrepared{};
    bool releaseRankPrepared{};
    int32_t formulaValue{};
    int32_t formulaValue2{};
    std::pair<uint64_t,uint64_t> formulaRandom{};
    bool hitReleased{};
    uint64_t infernoUntil{};
    uint64_t infernoEndAt{};
    std::optional<Vec> leapDestination{};
    std::optional<int> nestClass{}; // St49's captured fallback; valid MonStats.spawn takes priority.
    MonsterSpawnParameters minionSpawn{}; // St62 captures class/mode before the release callback.
    std::optional<MonsterTentacleExecution> tentacles{};
    std::shared_ptr<effects::CorpseExplosionPulse> corpseExplosion{};
    std::shared_ptr<effects::NativeAreaPulse> mosquito{};
    std::shared_ptr<effects::NativeAreaPulse> directSkill{};
    int mosquitoLoops{};
    bool nativeCallbackStarted{},cancelled{};
    bool pd2CastEventPosted{};
    std::shared_ptr<SkillRelease> nativeSkill{};
};
}
