#pragma once
#include "server/runtime/simulation.hpp"
#include "server/runtime/events.hpp"
#include "server/area_store.hpp"
#include "server/systems/ai/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/skills/system.hpp"
#include <array>
#include "server/systems/objects/system.hpp"
#include "server/systems/quests/state.hpp"

namespace d2x::server {
// Scheduler-thread diagnostic values. Never used as client replication or saved.
struct DiagnosticCommand {
    uint64_t sequence{}, tick{};
    PlayerId player;
    CommandResult result;
};
struct DiagnosticItem {
    EntityId id;
    uint64_t revision{};
    std::string code;
    ItemLocation location;
    unsigned quantity{}, durability{};
};
struct DiagnosticMonster {
    EntityId id;
    std::string code;
    RegionId area{};
    Vec position;
    int64_t life{}, maximumLife{};
    uint64_t revision{}, busyUntil{};
    bool moving{}, running{}, rewardComplete{};
    std::optional<ai::Controller> controller;
    uint64_t chilledUntil{},frozenUntil{},knockedUntil{},nextHitTick{};
    std::optional<PlayerId> owner{};
    MonsterAiProfile rules{};
    int damageRegen{}, threat{}, nativeVelocity{}, movementMask{}, spawnMask{}, blockChance{};
    int nestSpawned{};
    uint64_t webUntil{}, interruption{};
    bool corpseUnavailable{}, shield{};
    std::array<uint8_t,16> components{}, componentCounts{};
    size_t componentVariants{};
    std::vector<int> attacks{}, skills{};
    MonsterIdentity identity{};
    std::optional<MonsterEnchantment> enchantment{};
    Vec home{};
    std::vector<Vec> skillPositions{};
};
struct DiagnosticEffect { int state{}; uint64_t expires{}; };
struct DiagnosticSnapshot {
    SystemMetrics stageMetrics{};
    uint64_t spatialTick{},spatialGeneration{},spatialQueries{},spatialCandidates{},spatialReturned{};
    size_t spatialUnits{},combatQueueDepth{};
    uint64_t combatTargetAttempts{};
    std::vector<DiagnosticEffect> effects;
    std::vector<PlayerCorpse> corpses;
    std::vector<objects::Object> objects;
    std::string merchantDeferred;
    size_t craftingPending{}; std::string craftingDeferred;
    unsigned denRemaining{}; bool denCleared{};
    size_t questPending{};std::string questDeferred,hirelingDeferred;
    std::array<int,5> cainStones{};unsigned activatedStones{};
    bool eclipse{},altarDestroyed{},journalRead{},tombOpen{},durielSlain{};
    uint64_t tombAt{};
    std::optional<uint16_t> staffTombOffset;
    quests::ActThreeState actThree;
    quests::ActFourState actFour;
    quests::ActFiveState actFive;
    std::vector<travel::Portal> portals;
    std::map<RegionId,float> waypoints;
    size_t healingQueued{}, manaQueued{}, lootPending{};
    std::string lootDeferred;
    size_t itemTriggersPending{};
    std::string itemTriggersDeferred;
    std::vector<ChargedSkill> chargedSkills;
    uint64_t tick{}, eventFirst{}, eventLast{}, commandFirst{}, commandLast{};
    size_t commandsQueued{}, eventsQueued{}, monsterCount{}, missileCount{}, itemCount{};
    PlayerSnapshot player;
    CharacterRecord record;
    PlayerContainers containers;
    AreaView area;
    std::vector<AreaView> areas;
    std::vector<DiagnosticItem> items;
    std::vector<DiagnosticMonster> monsters;
    std::vector<skills::Cast> casts;
    std::vector<missiles::Missile> missiles;
    std::vector<combat::Damage> damage;
    size_t spellImpacts{}, spellTargets{}, pendingReleases{};
    std::optional<travel::Transition> travel;
    std::vector<Vec> path;
    std::vector<DiagnosticEvent> events;
    std::vector<DiagnosticCommand> commands;
};
}
