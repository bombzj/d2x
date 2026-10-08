#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/state.hpp"
#include <map>
#include <string>
#include <vector>

namespace d2x::server::transactions {
// Only cross-domain commit boundary; reservation/validation/commit must be atomic.
struct ItemTransfer { std::optional<PlayerId> from, to; ItemHandle item; ItemDestination destination; };
struct Reward { PlayerId player; uint64_t sourceOccurrence{}, experience{}; unsigned gold{}; };
struct Exchange { PlayerId first, second; std::vector<ItemTransfer> items; unsigned firstGold{}, secondGold{}; };
struct InventoryEdit {
    ActorContext actor;
    uint64_t expectedRevision{}, expectedCharacterRevision{};
    InventoryState inventory;
    std::vector<ItemChange> changes;
    unsigned weaponSet{};
};
enum class ResourceRefresh { Clamp, AttributeGain, LevelUp };
struct CharacterEdit {
    ActorContext actor;
    uint64_t expectedInventoryRevision{}, expectedCharacterRevision{};
    CharacterRecord player;
    ResourceRefresh resources = ResourceRefresh::Clamp;
    uint64_t experienceAward{};
};
using Change = std::variant<ItemTransfer, Reward, Exchange, InventoryEdit, CharacterEdit>;
struct PreparedPlayer {
    PersistentCharacter persistent;
    attributes::Totals totals;
    std::vector<ItemChange> changes;
    bool switchedWeapons{}, inventoryChanged{};
};
struct Plan { TransactionId id; std::vector<RevisionGuard> expected; Change change; std::optional<PreparedPlayer> player; };
// Local plan identities, never client retry tokens. No unbounded replay set.
struct State { uint64_t next = 1, lastCommitted{}; };
struct Ports { PlayerStore &players; const AreaStore &areas; EventOutbox &events; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<Plan> prepare(Change);
    DomainResult<> commit(Plan);
    // Atomic mana debit and optional same-area relocation at the release frame.
    DomainResult<> release(const ActorContext &, uint64_t expectedCharacterRevision, float manaCost,
                           std::optional<PointTarget> relocation = {});
    // Resource-only commit: no inventory clone or equipment re-evaluation per hit.
    DomainResult<> damage(const ActorContext &, uint64_t expectedCharacterRevision, int64_t amount);
};
}
