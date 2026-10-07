#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/quest/id.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::quests {
// Game quest triggers only; private persistent progress stays in PlayerStore.
enum class Action { Refresh, Acknowledge, ClaimReward };
struct Request { Action action; QuestId quest; std::optional<EntityId> npc; std::optional<uint32_t> message; };
struct GameQuest { uint64_t revision{}; std::set<EntityId> participants; };
struct State { std::map<QuestId, GameQuest> game; };
struct Ports { const PlayerStore &players; const objects::System &objects; transactions::System &transactions; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
};
}
