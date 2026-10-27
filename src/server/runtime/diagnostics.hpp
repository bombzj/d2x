#pragma once
#include "server/game_messages.hpp"
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
#include "gameplay/skills/stat_callbacks.hpp"
#include "gameplay/combat/alignment.hpp"
#include "server/systems/monsters/curse_control.hpp"
#include "gameplay/events/frame_queue.hpp"

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
    bool burrowed{},airborne{},holdPose{},skillMotion{};
    uint8_t aiPose{1};uint64_t poseUntil{};
    unsigned admittedPlayers{1};uint64_t experienceBase{};
    int minimumLife{},maximumBaseLife{};
    int64_t aiRegenerationBonus{};
    int drain{}, physicalResistance{}, difficulty{};
    int64_t woundRate{};
    uint64_t woundUntil{}, woundNext{};
    EntityId woundSource{}, woundCredit{};
    std::vector<ChargedSkill> chargedSkills{};
    std::vector<ItemAuraTimer> itemAuras{};
    std::vector<ItemStatEvent> itemStatEvents{};
    std::vector<int32_t> itemStatEventValues{};
    bool chargesTracked{};
    UnitAlignment alignment{};
    uint64_t allegianceRevision{};
    monsters::CurseControl curseControl{};
    EntityId attractionTarget{};
    uint64_t attractionUntil{};
    std::vector<monsters::AiResetTimer> aiResetTimers{};
    bool aiResetsTracked{};
    uint64_t specialTargetOrder{},wanderTargetOrder{},aiFunctionRevision{};
    bool detachedMinion{};
    struct Assassin {
        std::array<int64_t,3> parameters{};
        uint64_t nextDecision{},expires{},removeAt{};
        EntityId lastCorpse{},missile{};
    };
    std::optional<Assassin> assassin{};
};
struct DiagnosticEffect { int state{}; uint64_t expires{}; };
struct DiagnosticAnimation { std::string mode; int frames{},startFrame{},speed{},duration{},action{}; };
struct DiagnosticCollision { int x{},y{}; uint16_t mask{},objectMask{}; bool playerClear{}; };
struct DiagnosticDensity {
    RegionId area;
    size_t room{},logical{};
    std::string phase,deferred;
    int definition{},trial{},search{},remaining{},partyRemaining{},champions{};
    int processedRooms{},successfulRooms{},uniqueCount{};
    EntityId leader;
    std::optional<size_t> preset{};
    std::string quest{};
    EntityId object{};
    unsigned occurrence{};
    int retryAttempts{},finalRetryAttempts{},birthTicks{};
};
struct DiagnosticWander {
    RegionId area;size_t room{};uint64_t epoch{};std::string phase,deferred;int search{},count{};
};
struct DiagnosticRoomRandom {
    RegionId area;size_t room{};bool loaded{};uint64_t revision{},creationOrder{};std::optional<uint64_t> random;
};
struct DiagnosticHirelingSlot {
    int seller{};uint64_t revision{};uint16_t name{};uint32_t seed{};bool available{},hired{};
};
struct DiagnosticSnapshot {
    struct Pd2Map {
        int controller{},phase{};uint32_t eventProgress{};
        bool sceneOpening{};unsigned sceneParticipants{};std::vector<PlayerId> sceneEntrants;
        struct Death {EntityId unit;int phase{},portalPhase{},portalStatus{-1};bool evaluated{};std::optional<Vec> portalPosition;};
        struct Preset {RegionId area;int definition{},marker{},binding{};Vec position;std::string deferred;};
        std::vector<Death> deaths;
        std::vector<Preset> presets;
    };
    std::optional<Pd2Map> pd2Map;
    uint64_t gameRandom{};
    std::map<RegionId,uint64_t> levelRandom;
    std::vector<DiagnosticRoomRandom> roomRandom;
    std::optional<uint64_t> objectControlRandom,monsterRegionRandom,npcControlRandom,questControlRandom;
    std::vector<DiagnosticHirelingSlot> hirelingRosters;
    std::map<RegionId,objects::ObjectRegionPopulation> objectPopulation;
    std::vector<DiagnosticDensity> densityPopulation;
    std::vector<DiagnosticWander> wanderPopulation;
    bool modifiersPending{};
    std::string modifierFailure;
    size_t monsterEquipmentPending{};
    std::string monsterEquipmentFailure;
    unsigned virtualPlayers{1};
    SystemMetrics stageMetrics{};
    uint64_t spatialTick{},spatialGeneration{},spatialQueries{},spatialCandidates{},spatialReturned{};
    size_t spatialUnits{},combatQueueDepth{};
    uint64_t combatTargetAttempts{};
    std::vector<DiagnosticEffect> effects;
    std::vector<DiagnosticAnimation> animations;
    uint64_t recoveryUntil{},lastBlockTick{};
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
    std::vector<ItemAuraTimer> itemAuras;
    uint64_t tick{}, eventFirst{}, eventLast{}, commandFirst{}, commandLast{};
    size_t commandsQueued{}, eventsQueued{}, monsterCount{}, missileCount{}, itemCount{};
    PlayerSnapshot player;
    CharacterRecord record;
    PlayerContainers containers;
    AreaView area;
    std::vector<AreaView> areas;
    std::vector<DiagnosticItem> items;
    std::vector<DiagnosticMonster> monsters;
    std::vector<MonsterSnapshot> npcs; // Borrowed actor projection, independent of the monster detail limit.
    std::vector<skills::Cast> casts;
    std::vector<missiles::Missile> missiles;
    struct MeleeContact {EntityId source,target;uint64_t impact{};};
    std::vector<MeleeContact> damage;
    size_t spellImpacts{}, spellTargets{}, pendingReleases{};
    std::optional<travel::Transition> travel;
    std::vector<Vec> path;
    std::vector<DiagnosticCollision> collision;
    std::vector<DiagnosticEvent> events;
    FrameEventQueueObservation frameEvents;
    std::vector<DiagnosticCommand> commands;
};
}
