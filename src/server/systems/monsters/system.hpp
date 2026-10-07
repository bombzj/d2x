#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::monsters {
// Sole owner of monster/NPC/companion actor runtime; content identity is preserved.
struct Admission { MonsterIdentity identity; std::optional<MonsterKind> implementation; RegionId area; Vec position; bool hostile{}; };
struct MoveRequest { EntityId actor; PointTarget destination; };
struct Actor { EntityId id; MonsterIdentity identity; std::optional<MonsterKind> implementation; RegionId area; Vec position; uint64_t revision{}; std::optional<PlayerId> owner; };
struct State { std::map<EntityId, Actor> actors; };
struct Ports { const AreaStore &areas; const spatial::System &spatial; const attributes::System &attributes; EntityIds &ids; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<EntityId> admit(const Admission &);
    DomainResult<> requestMove(const MoveRequest &);
    DomainResult<> remove(EntityId);
    StepStatus step(TickContext, FrameFacts &);
};
}
