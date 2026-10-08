#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/state.hpp"
#include "server/systems/items/system.hpp"
#include <map>
#include <string>
#include <vector>

namespace d2x::server::transactions {
// Only cross-domain commit boundary; reservation/validation/commit must be atomic.
struct ItemTransfer { std::optional<PlayerId> from, to; ItemHandle item; ItemDestination destination; };
struct Reward { PlayerId player; uint64_t sourceOccurrence{}, experience{}; unsigned gold{}; };
struct Exchange { PlayerId first, second; std::vector<ItemTransfer> items; unsigned firstGold{}, secondGold{}; };
enum class ResourceRefresh { Clamp, AttributeGain, LevelUp };
struct WorldEdit { uint64_t expected{}; items::State next; };
struct InventoryEdit {
    ActorContext actor;
    uint64_t expectedRevision{}, expectedCharacterRevision{};
    InventoryState inventory;
    std::vector<ItemChange> changes;
    unsigned weaponSet{};
    std::shared_ptr<const EquipmentRules> equipment{};
    std::optional<WorldEdit> world{};
    std::optional<CharacterRecord> character{};
    std::optional<TransientAttributes> transient{};
    std::optional<std::vector<PlayerCorpse>> corpses{};
    ResourceRefresh resources = ResourceRefresh::Clamp;
    std::vector<DomainFact> facts{};
    std::vector<DomainFact> publicFacts{};
};
struct CharacterEdit {
    ActorContext actor;
    uint64_t expectedInventoryRevision{}, expectedCharacterRevision{};
    CharacterRecord player;
    ResourceRefresh resources = ResourceRefresh::Clamp;
    uint64_t experienceAward{};
    std::optional<TransientAttributes> transient{};
    std::optional<PointTarget> revival{};
    std::optional<std::map<RegionId,float>> waypoints{};
    std::optional<bool> selectedHand{};
    std::vector<DomainFact> publicFacts{};
    std::vector<DomainFact> facts{};
    std::optional<PointTarget> knockback{};
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
struct Ports { PlayerStore &players; const AreaStore &areas; EventOutbox &events; items::System &items; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<Plan> prepare(Change);
    DomainResult<> commit(Plan);
    // Same-area skill buffs may debit a caster and update a different player.
    // This restricted character-only group publishes all projections before swaps.
    DomainResult<> commitCharacters(std::vector<Plan>);
    // Atomic mana debit and optional same-area relocation at the release frame.
    DomainResult<> release(const ActorContext &, uint64_t expectedCharacterRevision, float manaCost,
                           std::optional<PointTarget> relocation = {});
    // Resource-only commit: no inventory clone or equipment re-evaluation per hit.
    DomainResult<> resources(const ActorContext &, uint64_t expected, float life, float mana, float stamina);
    DomainResult<> damage(const ActorContext &, uint64_t expectedCharacterRevision, int64_t amount);
};
}
