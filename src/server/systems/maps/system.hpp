#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "gameplay/events/frame_queue.hpp"
#include "server/systems/monsters/pd2/scene_pack.hpp"
#include "server/systems/population/system.hpp"
#include <map>
#include <array>
#include <vector>

namespace d2x::server::monsters {struct Actor;}
namespace d2x::server::maps {
struct Death {
    enum class Phase { Counter,Portal,Reward,Wait,Cleanup,Complete } phase{};
    uint64_t occurrence{};
    std::optional<Vec> portalPosition;
    std::optional<size_t> reward;
    bool rewardPositionPrepared{},rewardContext{};
    std::optional<PointTarget> rewardPosition;
    int remaining{-1};bool suppressTreasure{},cleanupPrepared{},evaluated{};
    std::vector<EntityId> cleanup;size_t cleanupIndex{};
    EntityId portalObject{};
    EntityId portalDestination{};
    int portalPhase{};
    int portalStatus{-1};
    std::optional<PointTarget> portalAnchor;
    std::optional<PointTarget> portalLanding;
    int tierRewardPhase{};
    bool tierRewardSelected{};
    int eventStep{},eventChoice{-1};
    bool eventWaiting{};
    int eventDestination{};
    std::vector<int> eventTreasures;
};
struct State {
    std::shared_ptr<const pd2::MapInstance> instance;
    std::map<EntityId,Death> deaths;
    std::map<EntityId,Death> eventDeaths;
    struct Binding {EntityId id;RegionId area;Vec position;bool object{};};
    std::array<Binding,10> bindings{};
    uint8_t phase{};
    int32_t pauseDepth{};
    uint8_t sceneCooldown{};
    struct PortalBirth {
        pd2::MapPortalProgram program;
        std::optional<PointTarget> landing{};EntityId destination{};
        int phase{};
        std::optional<PointTarget> anchor{};
    };
    std::map<EntityId,PortalBirth> portals;
    struct TimedTreasure {std::string treasure;int remaining{60};bool waiting{};RegionId area{};Vec position{};bool retained{};};
    std::map<std::pair<EntityId,uint64_t>,TimedTreasure> timedTreasures;
    std::map<std::pair<EntityId,uint64_t>,TimedTreasure> objectTreasures;
    uint32_t eventProgress{};
    struct RoomEvent {
        uint64_t generation{};
        bool planned{},done{},skip{},selected{},positionPrepared{},progressCommitted{};
        int definition{-1},type{},phase{},step{},remaining{};
        Vec center,requested;
        std::optional<PointTarget> position;
        EntityId source;
        Death portal;
    };
    std::map<std::pair<RegionId,size_t>,RoomEvent> roomEvents;
    struct EventOperation {uint32_t stage{};bool active{};int phase{},attempt{};bool positionPrepared{};std::optional<PointTarget> position;EntityId monster;int remaining{-1};Death reward;};
    std::map<EntityId,EventOperation> eventOperations;
    bool taintedEnding{};
    Death eventCleanup;
    struct InvaderBirth {int phase{},selection{};bool positionPrepared{};std::optional<PointTarget> position;EntityId child;};
    std::map<EntityId,InvaderBirth> invaderBirths;
    struct MendelnWave {
        bool planned{},positionPrepared{};
        std::array<int,3> counts{};
        int group{},index{},definition{-1};
        std::optional<PointTarget> position;
        monsters::Pd2ScenePack pack;
    };
    std::map<EntityId,MendelnWave> mendelnWaves;
    struct WandererBirth {
        RegionId area{};Vec source;
        std::optional<PointTarget> position;
        bool positionPrepared{},selected{},actBoss{},complete{};
        int attempt{},definition{-1};EntityId child;
        population::QuestBirth preset;
    };
    std::map<std::pair<EntityId,uint64_t>,WandererBirth> wandererBirths;
    struct SceneUnit {int definition{};bool object{};Vec global;int mode{},binding{-1};EntityId id{};std::vector<Vec> fallback{};size_t attempt{};};
    struct SceneBirth {std::vector<SceneUnit> units;size_t next{};bool linked{};};
    std::map<RegionId,SceneBirth> sceneBirths;
    std::map<EntityId,Death> sceneRetreats;
    Death sceneTownPortal,sceneTownReturn;
    size_t sceneTownStage{};
    std::map<PlayerId,EntityId> sacrificeUsers;
};
struct Ports {
    const PlayerStore &players;const AreaStore &areas;monsters::System &monsters;
    travel::System &travel;world::System &world;loot::System &loot;
    transactions::System &transactions;uint32_t &random;const spatial::System &spatial;
    objects::System &objects;
    EventOutbox &events;
    FrameEventQueue &frameEvents;
    population::System &population;
    effects::System &effects;
};
class System {
    State state_;
    const Ports ports_;
    bool handles(const monsters::Actor &) const;
    bool handlesEventDeath(const monsters::Actor &) const;
    bool controllerDeathReady(const monsters::Actor &) const;
    DomainResult<> advanceDeath(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceRandomRewards(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceCharmReward(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceImperialReward(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceOuterVoidDeath(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceTierBossRewards(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceDeathPortal(RegionId,Death &,const pd2::MapPortalProgram &,int,Vec,int,TickContext);
    DomainResult<PointTarget> preparePortalLanding(const pd2::MapPortalProgram &,std::optional<PointTarget> &,std::optional<PointTarget> &);
    void prepareDropPosition(const monsters::Actor &,Death &,bool requireSearch=false) const;
    void prepareDropPosition(RegionId,Vec,Death &,bool requireSearch=false) const;
    std::optional<PointTarget> prepareScenePosition(RegionId,Vec,int size,int radius=8,bool offset=true,uint16_t mask=0x805) const;
    void synchronize();
    bool advancePortals(TickContext);
    void synchronizeTimedTreasures(TickContext);
    DomainResult<> notify(std::string_view,uint8_t,TickContext);
    DomainResult<> endTaintedSun(TickContext);
    DomainResult<> advanceEventOperation(EntityId,TickContext);
    bool advanceEventOperations(TickContext);
    bool advanceEventDeaths(TickContext);
    DomainResult<> advanceEventDeath(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceInvaderDeath(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceHorazonDeath(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceEndgameDeath(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceBossSceneDeath(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceAncientsDeath(const monsters::Actor &,Death &,TickContext);
    DomainResult<> initializeSceneRoom(RegionId,TickContext);
    DomainResult<> initializeSceneTown(TickContext);
    DomainResult<> removeSceneWaypoints();
    DomainResult<> advanceAncientsOperation(EntityId,TickContext);
    DomainResult<> eventTreasure(const monsters::Actor &,Death &,int offset,TickContext);
public:
    explicit System(Ports ports):ports_(ports) {}
    const State &read() const {return state_;}
    void scenePhase(uint8_t phase) {state_.phase=phase;}
    void sceneCooldown(uint8_t value) {state_.sceneCooldown=value;}
    void consumeScenePause() {if(state_.pauseDepth>0) --state_.pauseDepth;}
    void sceneRoomRelease(RegionId,size_t index);
    DomainResult<> sceneEclipse(RegionId,bool,TickContext);
    DomainResult<> rathmaRetreatPortal(EntityId,TickContext);
    std::optional<PointTarget> bossSpawnPosition(RegionId,Vec,bool search) const;
    DomainResult<EntityId> bossSpawn(const PointTarget &,int definition,std::string key,TickContext);
    void bind(int slot,EntityId,RegionId,Vec,bool object);
    int selectPreset(int marker);
    DomainResult<> operateTome(const ActorContext &,EntityId);
    DomainResult<> treasureEvent(const FrameEvent &,TickContext);
    DomainResult<> wandererEvent(const FrameEvent &,TickContext);
    DomainResult<> objectTreasureEvent(const FrameEvent &,TickContext);
    DomainResult<bool> populateRoom(RegionId,size_t,int processed,TickContext);
    DomainResult<> operateEvent(const ActorContext &,EntityId);
    DomainResult<> operateAncients(const ActorContext &,EntityId);
    DomainResult<> operateSacrifice(const ActorContext &,EntityId);
    DomainResult<> closeSacrifice(const ActorContext &,EntityId);
    bool sacrificeAccess(const ActorContext &,EntityId,ItemHandle) const;
    void completeSacrifice(const ActorContext &,EntityId);
    DomainResult<> invaderWave(EntityId,TickContext);
    DomainResult<> mendelnWave(EntityId,TickContext);
    bool advanceMendelnPhase(uint32_t expected) {
        if(!state_.instance || state_.instance->controller!=11 || state_.eventProgress!=expected) return false;
        ++state_.eventProgress;return true;
    }
    void objectRestored(EntityId previous,EntityId current);
    bool opensBlockerOnBirth(int definition) const {
        return state_.instance && state_.instance->controller==9 && state_.phase==2 && definition>=616 && definition<=619;
    }
    bool deathReady(const monsters::Actor &) const;
    bool skipsTreasure(const monsters::Actor &) const;
    StepStatus step(TickContext,FrameFacts &);
};
}
