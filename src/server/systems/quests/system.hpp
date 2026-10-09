#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/npc_rules.hpp"
#include "gameplay/quest/id.hpp"
#include "preparation.hpp"
namespace d2x::server::quests {
enum class Action { Refresh, Acknowledge, ClaimReward, ClaimRespec };
struct Request { Action action; QuestId quest; std::optional<EntityId> npc; std::optional<uint32_t> message; };
struct State {
    bool denCleared{};
    unsigned denRemaining{};
    std::set<PlayerId> eligible;
    std::map<PlayerId,unsigned> observed;
    std::array<bool,6> objectives{};
    std::map<QuestId,std::set<PlayerId>> goals;
    std::array<int,5> stones{};
    unsigned activatedStones{};
    bool stonesOrdered{}, cainRescued{};
    bool restoreCairnStones{}, cainPortalOpened{};
    bool andarielPortal{};
    uint64_t andarielPortalAt{};
    std::map<PlayerId,Preparation> pending;
    uint64_t next=1;
    std::string deferred;
    std::map<EntityId,bool> completed;
};
struct Ports { const PlayerStore &players; const AreaStore &areas; const population::System &population; const monsters::System &monsters; const npc::System &npc; transactions::System &transactions; EventOutbox &events; const GameSettings &settings; items::System &items; travel::System &travel; world::System &world; uint64_t &random; };
struct Dialogue {QuestId quest; NpcMessage message;};
class System {
    State state_; const Ports ports_;
    DomainResult<> commit(const ActorContext &,CharacterRecord);
    DomainResult<> prepareReward(const ActorContext &,EntityId,RewardKind,bool conversation);
    StepStatus denStep(TickContext);
    StepStatus actOneStep(TickContext);
    void orderStones();
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    std::optional<Dialogue> dialogue(const ActorContext &,const NpcRule &) const;
    DomainResult<> operate(const ActorContext &,EntityId,int definition,int operation,Vec);
    std::vector<Preparation> pending() const;
    DomainResult<> install(Prepared);
    std::optional<bool> takeCompletion(EntityId);
    bool npcVisible(const CharacterRecord &,std::string_view code,RegionId) const;
    DomainResult<> execute(const ActorContext &,const Request &);
    StepStatus step(TickContext,FrameFacts &);
};
}
