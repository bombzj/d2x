#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::spatial {
// Derived spatial index only; actor positions stay with their owner.
enum class UnitKind { Player, Monster, Object, Missile, Item };
struct UnitRef { EntityId id; UnitKind kind; uint64_t revision{}; };
struct Query { RegionId area; Vec center; float radius{}; };
struct State { uint64_t indexedTick{}, generation{}; size_t indexedUnits{}; mutable uint64_t queries{}, candidates{}, returned{}; };
struct Ports { const AreaStore &areas; const PlayerStore &players; const monsters::System &monsters; const objects::System &objects; const missiles::System &missiles; const items::System &items; };
class System {
    State state_;
    const Ports ports_;
  struct Entry { UnitRef unit; Vec position; };
  struct Region { std::map<std::pair<int,int>,std::vector<Entry>> cells; int maximumSize{}; };
  std::map<RegionId,Region> regions_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<std::vector<UnitRef>> query(const Query &) const;
    int maximumSize(RegionId) const;
    StepStatus step(TickContext, FrameFacts &);
};
}
