#pragma once
#include "server/game_messages.hpp"
#include <optional>
#include <utility>
#include <variant>

namespace d2x::server {
// Room-owned configuration. A future participant's save cannot change it.
struct GameSettings { uint32_t mapSeed{}; int difficulty{}; };
// Kernel results are not wire replies. A scaffold never manufactures a value.
enum class DomainStatus { Applied, NotImplemented, InvalidActor, Stale, InvalidRequest, Unavailable, Conflict, Capacity };
template<class T = std::monostate> struct DomainResult {
    DomainStatus status = DomainStatus::NotImplemented;
    std::optional<T> value;
    explicit operator bool() const { return status == DomainStatus::Applied && value.has_value(); }
};
struct TickContext {
    uint64_t tick{};
    static constexpr float seconds = 1.f / 25.f;
};
struct ActorContext {
    PlayerId player;
    EntityId actor;
    RegionId area;
    uint64_t areaGeneration{}, sequence{}, tick{};
};
struct UnitTarget { EntityId id; uint64_t revision{}; };
struct PointTarget { RegionId area; uint64_t generation{}; Vec position; };
using ActionTarget = std::variant<UnitTarget, PointTarget>;
struct TransactionId {
    uint64_t value{};
    auto operator<=>(const TransactionId &) const = default;
};
// Entity IDs never repeat within an instance; instance identity stays in the
// host binding. Revisions protect operations on objects that can change in place.
struct RevisionGuard { EntityId entity; uint64_t expected{}; };
enum class StepStatus { Complete, NotImplemented, Blocked };
}
