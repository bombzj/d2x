#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/npc_rules.hpp"
#include "gameplay/quest/id.hpp"
namespace d2x::server::quests {
enum class Action { Refresh, Acknowledge, ClaimReward };
struct Request { Action action; QuestId quest; std::optional<EntityId> npc; std::optional<uint32_t> message; };
struct State {
    bool denCleared{};
    unsigned denRemaining{};
    std::set<PlayerId> eligible;
    std::map<PlayerId,unsigned> observed;
};
struct Ports { const PlayerStore &players; const AreaStore &areas; const population::System &population; const monsters::System &monsters; const npc::System &npc; transactions::System &transactions; EventOutbox &events; const GameSettings &settings; };
class System {
    State state_; const Ports ports_;
    DomainResult<> commit(const ActorContext &,CharacterRecord);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    std::optional<NpcMessage> dialogue(const ActorContext &,const NpcRule &) const;
    DomainResult<> execute(const ActorContext &,const Request &);
    StepStatus step(TickContext,FrameFacts &);
};
}
