#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/area_store.hpp"
#include "gameplay/npc/intents.hpp"
namespace d2x::server::npc {
using Intent = std::variant<TalkToNpc, EndNpcConversation>;
struct Request { Intent intent; };
struct Conversation { EntityId npc; RegionId area{}; uint64_t revision{}; std::optional<uint16_t> pendingMessage; bool quest{}; EntityId actor{}; uint64_t areaGeneration{}; QuestId questId=QuestId::DenOfEvil; };
struct State { std::map<PlayerId, Conversation> conversations; uint64_t next = 1; };
struct Ports { const PlayerStore &players; const AreaStore &areas; transactions::System &transactions; EventOutbox &events; const GameSettings &settings; quests::System &quests; effects::System &effects; };
class System {
    State state_; const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    const AreaNpc *find(const ActorContext &, EntityId, bool conversation = false) const;
    const Conversation *conversation(PlayerId) const;
    DomainResult<> execute(const ActorContext &, const Request &);
    DomainResult<> close(PlayerId);
    StepStatus step(TickContext, FrameFacts &);
};
}
