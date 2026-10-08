#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/object_rules.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::objects {
// Runtime doors/chests/shrines/wells/quest objects; resource-only map decorations are separate.
struct Request { UnitTarget target; };
struct Admission { int definition{}; RegionId area; Vec position; };
struct Object { EntityId id; int definition{}; RegionId area; Vec position; uint64_t revision{}; int mode{}; ObjectRule rule; uint64_t until{}, reset{}; bool pending{}; int uses{}; };
struct State { std::map<EntityId, Object> objects; };
struct Ports { const PlayerStore &players; const AreaStore &areas; quests::System &quests; loot::System &loot; effects::System &effects; transactions::System &transactions; EntityIds &ids; world::System &world; const monsters::System &monsters; const GameSettings &settings; inventory::System &inventory; travel::System &travel; skills::System &skills; EventOutbox &events; };
class System {
    State state_;
    const Ports ports_;
    void collision(RegionId);
    DomainResult<> shrine(const ActorContext &, const Object &);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<EntityId> admit(const Admission &);
    DomainResult<> execute(const ActorContext &, const Request &, std::optional<int> remoteRange = {});
    DomainResult<> openMonsterDoor(EntityId,Vec destination,uint64_t tick);
    StepStatus step(TickContext, FrameFacts &);
};
}
