#pragma once
#include "system.hpp"
#include "druid_active.hpp"
#include "server/systems/actions/release.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/actions/monster_timing.hpp"

namespace d2x::server::skills {
struct Pd2VengeanceTimers;
struct System::Runtime {
    std::shared_ptr<Pd2VengeanceTimers> pd2Vengeance;
    struct Pending { ActorContext actor; Request request; };
    std::map<PlayerId,Pending> pending;
    struct ImpEjection {EntityId rider;std::optional<Vec> landing;bool prepared{},interrupted{},kill{},complete{};};
    std::map<EntityId,ImpEjection> impEjections;
    uint64_t nativeKickOccurrence{};
    std::map<EntityId,Release> assassinStarts,druidStarts,weaponStarts;
    struct MonsterWeaponStart {uint64_t tick{};uint16_t skill{};uint8_t mode{};MonsterAttackRule program;};
    std::map<EntityId,MonsterWeaponStart> monsterWeaponStarts;
    struct MonsterLeapStart {
        uint64_t tick{},occurrence{};EntityId target;MonsterAttackRule attack;
        Vec targetPosition,landing;unsigned stage{};
    };
    std::map<EntityId,std::shared_ptr<MonsterLeapStart>> monsterLeapStarts;
    struct MonsterWhirlwindStart {uint64_t tick{};uint16_t skill{};Vec destination;unsigned stage{};};
    std::map<EntityId,MonsterWhirlwindStart> monsterWhirlwindStarts;
    struct MonsterLeapAttackStart {uint64_t tick{};uint16_t skill{};Vec landing{};unsigned stage{};};
    std::map<EntityId,MonsterLeapAttackStart> monsterLeapAttackStarts;
    std::map<FrameEventHandle,std::shared_ptr<DruidActiveExecution>> druidActive;
    struct MonsterMode {
        MonsterAttackRule program;
        uint16_t skill{};
        MonsterAnimationCursor animation;
        uint64_t identity{},interruption{},lastFrame{},movementFrame{};
        bool advanced{},frozenSequence{},movementAdvanced{},runStopped{};
        MonsterAnimationStep step;
        std::optional<ActionTimeline> pd2Rewind;
        uint64_t pd2RewindStarted{};
        size_t pd2RewindCursor{};
        std::optional<MonsterRelease> repeated;
        std::optional<uint64_t> whirlwindContact;
        std::optional<EntityId> whirlwindTarget;
        uint32_t whirlwindPrevious{UINT32_MAX};
        bool whirlwindStopped{},whirlwindRolled{},whirlwindHit{},whirlwindAlternate{};
        struct LeapAttack {
            Vec landing;uint16_t flags{0x1080};uint64_t endAt{};
            std::optional<uint64_t> contact{};std::optional<EntityId> target{};
            bool landed{},statesCleared{};
        };
        std::optional<LeapAttack> leapAttack;
        struct Pd2Dash {
            uint16_t flags{0x80};bool movingState{},landed{};
            std::shared_ptr<missiles::NativeBirthPublication> birth;
            Vec callbackPosition;int callbackSlot{-1};bool motionStarted{};
        };
        std::optional<Pd2Dash> pd2Dash;
    };
    std::map<EntityId,MonsterMode> monsterModes;
    std::map<EntityId,Release> &releases;
    std::map<EntityId,MonsterRelease> &monsters;
    Runtime(std::map<EntityId,Release> &queue, std::map<EntityId,MonsterRelease> &monsterQueue)
        : releases(queue), monsters(monsterQueue) {}
};
}
