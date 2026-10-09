#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/npc_rules.hpp"
#include "gameplay/quest/id.hpp"
#include "preparation.hpp"
#include "state.hpp"
namespace d2x::server::quests {
enum class Action { Refresh, Acknowledge, ClaimReward, ClaimRespec, StaffUpdate, ReadJournal };
struct Request { Action action; QuestId quest; std::optional<EntityId> npc; std::optional<uint32_t> message; std::optional<EntityId> item{}; };
struct State {
    ActOneState actOne;
    ActTwoState actTwo;
    ActThreeState actThree;
    ActFourState actFour;
    ActFiveState actFive;
    std::array<bool,size_t(QuestId::Count)> objectives{};
    std::map<QuestId,Goal> goals;
    std::map<PlayerId,Preparation> pending;
    uint64_t next=1;
    std::string deferred;
    std::map<EntityId,bool> completed;
    std::map<EntityId,int> objectModes;
};
struct Ports { const PlayerStore &players; const AreaStore &areas; const population::System &population; monsters::System &monsters; npc::System &npc; transactions::System &transactions; EventOutbox &events; const GameSettings &settings; items::System &items; travel::System &travel; world::System &world; uint64_t &random; loot::System &loot; effects::System &effects; };
struct Dialogue {QuestId quest; NpcMessage message;};
class System {
    State state_; const Ports ports_;
    DomainResult<> commit(const ActorContext &,CharacterRecord,bool levelUp=false);
    QuestFact fact(PlayerId,const CharacterRecord &,RegionId) const;
    DomainResult<> prepareReward(const ActorContext &,EntityId,RewardKind,bool conversation,std::optional<Vec> dropPosition = {});
    StepStatus denStep(TickContext);
    StepStatus actOneStep(TickContext);
    StepStatus actTwoStep(TickContext);
    StepStatus actThreeStep(TickContext);
    StepStatus actFourStep(TickContext);
    StepStatus actFiveStep(TickContext);
    StepStatus ancientsStep(TickContext);
    StepStatus baalStep(TickContext);
    StepStatus flushGoals(TickContext);
    void captureGoal(QuestId,uint32_t,RegionId);
    std::optional<Dialogue> actOneDialogue(const ActorContext &,const NpcRule &) const;
    std::optional<Dialogue> actTwoDialogue(const ActorContext &,const NpcRule &) const;
    DomainResult<> acknowledgeActTwo(const ActorContext &,const Request &,const NpcRule &);
    DomainResult<> operateActTwo(const ActorContext &,EntityId,int,int,Vec);
    DomainResult<> submitStaff(const ActorContext &,EntityId,EntityId,unsigned);
    std::optional<Dialogue> actThreeDialogue(const ActorContext &,const NpcRule &) const;
    DomainResult<> acknowledgeActThree(const ActorContext &,const Request &,const NpcRule &);
    std::optional<Dialogue> speech(const NpcRule &,QuestId,std::string_view,uint8_t menu=0) const;
    DomainResult<> spawnQuestGroup(const ActorContext &,std::string_view);
    DomainResult<> operateActThree(const ActorContext &,EntityId,int,int,Vec);
    std::optional<Dialogue> actFourDialogue(const ActorContext &,const NpcRule &) const;
    DomainResult<> acknowledgeActFour(const ActorContext &,const Request &,const NpcRule &);
    DomainResult<> operateActFour(const ActorContext &,EntityId,int,int,Vec);
    std::optional<Dialogue> actFiveDialogue(const ActorContext &,const NpcRule &) const;
    DomainResult<> acknowledgeActFive(const ActorContext &,const Request &,const NpcRule &);
    DomainResult<> operateActFive(const ActorContext &,EntityId,int,int,Vec);
    void resetAncients();
    void orderStones();
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    std::optional<Dialogue> dialogue(const ActorContext &,const NpcRule &) const;
    DomainResult<> operate(const ActorContext &,EntityId,int definition,int operation,Vec);
    std::vector<Preparation> pending() const;
    DomainResult<> install(Prepared);
    std::optional<bool> takeCompletion(EntityId);
    bool npcVisible(const CharacterRecord &,std::string_view code,RegionId,int initFunction=0) const;
    DomainResult<> execute(const ActorContext &,const Request &);
    bool allowsTravel(const CharacterRecord &,RegionId,RegionId) const;
    std::optional<int> objectMode(RegionId,EntityId,int definition,int operation,uint64_t tick) const;
    void onTownPortal(RegionId region) {if(int(region)==120) resetAncients();}
    StepStatus step(TickContext,FrameFacts &);
};
}
