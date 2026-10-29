#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/npc_rules.hpp"
#include "gameplay/quest/id.hpp"
#include "preparation.hpp"
#include "state.hpp"
#include "npc_story.hpp"
#include "prisoner_death.hpp"
#include <deque>
#include <span>
namespace d2x::server::reactions { class ItemTriggers; }
namespace d2x::server::monsters {struct Actor;}
namespace d2x::server::quests {
enum class Action { Refresh, ViewLog, Acknowledge, ClaimReward, ClaimRespec, StaffUpdate, ReadJournal, ReadAncientAltar, ReadClue };
struct Request { Action action; QuestId quest; std::optional<EntityId> npc; std::optional<uint32_t> message; std::optional<EntityId> item{}; };
struct State {
    NpcStoryState npcStory;
  std::optional<uint64_t> controlRandom;
    ActOneState actOne;
    ActTwoState actTwo;
    ActThreeState actThree;
    ActFourState actFour;
    ActFiveState actFive;
    std::array<bool,size_t(QuestId::Count)> objectives{};
    std::array<uint16_t,48> initialFlags{}, publishedFlags{};
    std::map<PlayerId,std::set<QuestId>> credited;
    std::map<PlayerId,EntityId> creditedActors;
    std::multimap<QuestId,Goal> goals;
    std::deque<std::pair<QuestId,StatusNotice>> statusNotices;
    std::set<QuestId> started;
    std::map<PlayerId,Preparation> pending;
    uint64_t next=1;
    std::string deferred;
    std::map<EntityId,bool> completed;
    std::map<EntityId,int> objectModes;
};
struct Ports { const PlayerStore &players; AreaStore &areas; population::System &population; monsters::System &monsters; npc::System &npc; transactions::System &transactions; EventOutbox &events; const GameSettings &settings; items::System &items; travel::System &travel; world::System &world; uint64_t &random; loot::System &loot; effects::System &effects; const social::System &social; reactions::ItemTriggers &itemTriggers; missiles::System &missiles; objects::System &objects; const spatial::System &spatial; };
struct Dialogue {QuestId quest; NpcMessage message;};
class System {
    State state_; const Ports ports_;
    struct FlagPublication {
        struct Recipient {EntityId actor;QuestFact fact;};
        std::array<uint16_t,48> flags{};
        std::vector<Recipient> recipients;
        size_t next{};
    };
    std::deque<FlagPublication> flagPublications_;
    size_t stepCursor_{};
    bool stepResuming_{},stepBlocked_{};
    bool completionPending() const {return state_.actOne.denCompletion.has_value() || state_.actFive.nihlathakCompletion.has_value() || state_.actFive.anyaCompletion.has_value() || state_.actFour.sanctumCompletion.has_value() || state_.actFour.sanctumSweep.has_value() || state_.actFour.diabloBirthPending;}
    DomainResult<> publishCompletion(const QuestCompletion &,uint8_t effect,std::optional<uint16_t> sound,TickContext);
    DomainResult<> commit(const ActorContext &,CharacterRecord,bool levelUp=false,std::optional<QuestId> primaryGoal={},bool notifyQuest=true);
    QuestFact fact(PlayerId,const CharacterRecord &,RegionId) const;
    DomainResult<> prepareReward(const ActorContext &,EntityId,RewardKind,bool conversation,std::optional<Vec> dropPosition = {});
    StepStatus denStep(TickContext);
    StepStatus actOneStep(TickContext);
    StepStatus actTwoStep(TickContext);
    StepStatus actThreeStep(TickContext);
    StepStatus orbStep(TickContext);
    StepStatus actFourStep(TickContext);
    StepStatus actFiveStep(TickContext);
    void updatePrisonerCounters() noexcept;
    void resolvePrisoners(std::optional<PlayerId>);
    unsigned resolvedPrisoners(const ActFiveState::RescuePortal &) const;
    StepStatus ancientsStep(TickContext);
    StepStatus baalStep(TickContext);
    StepStatus flushGoals(TickContext);
    StepStatus flushStatuses(TickContext);
    void captureGoal(QuestId,uint32_t,RegionId,std::optional<Vec> = {},std::optional<PlayerId> = {});
    void captureGoal(QuestId,uint32_t,std::set<PlayerId>,bool attributeReward=false,bool consumeForgeItems=false);
    void shareProgress(QuestId,uint32_t,std::set<PlayerId>,bool notifyQuest=true,uint32_t flags=0);
    std::set<PlayerId> progressRecipients(QuestId) const;
    DomainResult<> startQuest(const ActorContext &,CharacterRecord,QuestId,EntityId,uint32_t flags=0);
    void queueStatus(QuestId,uint8_t,std::optional<EntityId> deactivateNpc={});
    void prepareStatusNotice(QuestId,StatusNotice &) const;
    DomainResult<bool> spawnNpcBluePortal(bool anya,bool town,RegionId,Vec observer,Vec global,uint64_t);
    DomainResult<> returnNpcToTown(bool anya,uint64_t,bool portal,uint64_t invocation=0);
    void queueGoal(QuestId,Goal);
    std::set<PlayerId> eligibleParty(QuestId,uint32_t,RegionId,std::optional<Vec>,std::optional<PlayerId>) const;
    std::set<PlayerId> eligibleDelivery(QuestId,uint32_t,PlayerId,bool allInAct=false) const;
    bool credited(PlayerId,QuestId) const;
    void creditPlayer(PlayerId,QuestId);
    bool rewardPending(const CharacterRecord &,QuestId) const;
    bool entitled(PlayerId,const CharacterRecord &,QuestId) const;
    bool unavailable(PlayerId,const CharacterRecord &,QuestId) const;
    std::optional<PlayerId> killerPlayer(EntityId) const;
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
    DomainResult<> readAncientAltar(const ActorContext &,EntityId);
    void orderStones();
    StepStatus npcStoryStep(TickContext);
    DomainResult<> bindNpcMapAi(EntityId);
    DomainResult<EntityId> spawnStoryNpc(RegionId,std::string_view,Vec,uint64_t,bool critical);
    DomainResult<std::optional<Vec>> npcSpawnPosition(const AreaNpc &,RegionId,uint64_t tick);
    bool npcInteracting(EntityId) const;
  public:
    void registerNpcStoryObject(EntityId,RegionId,int definition,int initialization,Vec);
    uint64_t cainBirthDue(EntityId) const;
    DomainResult<> cainBirthEvent(EntityId,uint64_t);
    DomainResult<bool> admitStoryNpc(const AreaNpc &,RegionId,uint64_t tick);
    DomainResult<NpcAiResult> npcThink(EntityId,uint64_t tick);
    void npcInteractionDecision(EntityId,uint64_t tick);
    uint64_t ancientStatueDue(EntityId) const;
    uint64_t ancientObjectAnimationEnd(EntityId) const;
    DomainResult<> ancientStatueEvent(EntityId,uint64_t);
    void finishAncientObjectAnimation(EntityId);
    explicit System(Ports ports) : ports_(ports) {}
    void initializeControl();
    DomainResult<> initializeZoo(RegionId,uint64_t tick);
    const State &read() const { return state_; }
    bool nativeCallbackPending() const {return stepResuming_ || completionPending();}
    void initialize(const CharacterRecord &);
    void nativeEliteBorn(EntityId,bool eligible);
    DomainResult<> activateSiegeBoss(EntityId,uint64_t tick);
    DomainResult<> activateIzual(uint64_t tick);
    uint64_t forgeRewardDue(EntityId) const;
    uint64_t forgeAnimationEnd(EntityId) const;
    DomainResult<> forgeRewardEvent(EntityId,uint64_t tick);
    DomainResult<> activateNihlathak(uint64_t tick);
    uint64_t prisonerPortalDue(EntityId) const;
    uint64_t npcPortalDue(EntityId) const;
    DomainResult<> npcPortalEvent(EntityId,uint64_t);
    struct PrisonerPortalView {EntityId object;RegionId area;Vec position;bool anyResolved{};};
    std::optional<PrisonerPortalView> prisonerPortal(EntityId) const;
    DomainResult<bool> activatePrisoner(EntityId,PlayerId,uint64_t tick);
    DomainResult<> finishPrisonerEscape(EntityId);
    void prisonerActivated();
    std::optional<PrisonerDeathPublication> preparePrisonerDeath(const monsters::Actor &,const std::map<EntityId,monsters::Actor> &,
        std::span<const EntityId>) const;
    void publishPrisonerDeath(PrisonerDeathPublication &) noexcept;
    DomainResult<> prisonerPortalEvent(EntityId,uint64_t tick);
    void rebindObject(EntityId previous,EntityId current);
    void registerCainObject(EntityId,int definition,int initialization,RegionId,Vec);
    void registerTownCain(EntityId);
    bool cainTownRequested() const;
    std::optional<int> initialObjectMode(RegionId,EntityId,int definition,int operation,int initialization,uint64_t tick) const;
    std::array<uint16_t,48> gameFlags() const;
    bool pendingGoals(PlayerId) const;
    QuestFact projection(PlayerId player,const CharacterRecord &record,RegionId area,bool fullStatus=true) const { auto result=fact(player,record,area);result.fullStatus=fullStatus;return result; }
    std::optional<Dialogue> dialogue(const ActorContext &,const NpcRule &) const;
    std::vector<Dialogue> dialogues(const ActorContext &,const NpcRule &) const;
    DomainResult<> operate(const ActorContext &,EntityId,int definition,int operation,Vec);
    std::vector<Preparation> pending() const;
    DomainResult<> install(Prepared);
    std::optional<bool> takeCompletion(EntityId);
    bool npcVisible(const CharacterRecord &,std::string_view code,RegionId,int initFunction=0) const;
    DomainResult<> execute(const ActorContext &,const Request &);
    void npcDeactivated(EntityId);
    bool allowsTravel(const CharacterRecord &,RegionId,RegionId) const;
    std::optional<int> objectMode(RegionId,EntityId,int definition,int operation,uint64_t tick) const;
    void onTownPortal(RegionId region) {if(int(region)==120) resetAncients();}
    DomainResult<> baalFx(RegionId,Vec);
    DomainResult<> spawnBaalWave(RegionId,Vec,int mappedPreset,uint64_t tick);
    DomainResult<> openWorldstoneChamber(EntityId);
    DomainResult<> baalThroneClassHints(EntityId,unsigned wave,uint64_t tick);
    DomainResult<> baalThroneSound(EntityId,uint64_t tick);
    DomainResult<> sealAfterglow(RegionId,EntityId,Vec,uint64_t tick);
    DomainResult<> cairnFx(RegionId,Vec,int destination,uint64_t tick);
    StepStatus step(TickContext,FrameFacts &);
};
}
