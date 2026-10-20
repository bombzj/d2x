#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <span>

namespace d2x::server {struct RoomView;}
namespace d2x {struct Grid;}

namespace d2x::server::spatial {
// Derived spatial index only; actor positions stay with their owner.
enum class UnitKind { Player, Monster, Object, Missile, Item };
struct UnitRef { EntityId id; UnitKind kind; uint64_t revision{}; };
struct Query { RegionId area; Vec center; float radius{}; };
struct State { uint64_t indexedTick{}, generation{}; size_t indexedUnits{}; mutable uint64_t queries{}, candidates{}, returned{}; };
struct Ports { const AreaStore &areas; const PlayerStore &players; const monsters::System &monsters; const objects::System &objects; const missiles::System &missiles; const items::System &items; const travel::System &travel; };
class System {
    State state_;
    const Ports ports_;
  struct Entry { UnitRef unit; Vec position; };
  struct Region { std::map<int,std::map<int,std::map<EntityId,Entry>>> cells; int maximumSize{}; };
  struct Location { RegionId area; int x{}, y{}; uint64_t generation{}; };
  std::map<RegionId,Region> regions_;
  std::map<EntityId,Location> locations_;
  void erase(EntityId, const Location &);
  std::function<uint16_t(int,int32_t,int32_t)> nativeFootprints(uint16_t,
      std::optional<std::span<const EntityId>>,EntityId,EntityId) const;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<std::vector<UnitRef>> query(const Query &) const;
    int maximumSize(RegionId) const;
    // Fresh synchronous footprint projection, independent of the last indexed
    // tick. A supplied unit span restricts actor footprints to that native room
    // chain. The returned sampler must not cross an authority publication.
    std::function<uint16_t(RoomView,int32_t,int32_t)> nativeCollisionSample(uint16_t mask=0xffff,
        std::optional<std::span<const EntityId>> units={},EntityId self={},EntityId target={}) const;
    Grid navigationGrid(RegionId,EntityId self={},EntityId target={}) const;
    StepStatus step(TickContext, FrameFacts &);
};
}
