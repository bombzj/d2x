#pragma once
#include <functional>
#include <span>
#include "plan.hpp"
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/state.hpp"
#include "server/systems/items/system.hpp"
#include <map>
#include <string>
#include <vector>

namespace d2x::server::actions {struct State;}
namespace d2x::server {struct PlayerState;}
namespace d2x::server::transactions {
// Local plan identities, never client retry tokens. No unbounded replay set.
struct State { uint64_t next = 1, lastCommitted{}; };
struct Ports { PlayerStore &players; AreaStore &areas; EventOutbox &events; items::System &items; actions::State &actions; effects::System &effects; uint64_t &random; };
class System {
    struct AuraPreparation;
    void prepareAuraRecipient(AuraPreparation &,const ActorContext &,const AuraDefinition &,const SkillAuraChange &,EntityId,int passiveState);
    DomainResult<Plan> prepareCandidate(Change,const PlayerState *,const InitialEffectMutation &,AuraPreparation &,bool admission=false);
    DomainResult<> commitGroup(std::vector<Plan>,std::optional<WorldEdit>,bool inventories,
        std::shared_ptr<effects::AuraUnitPublication> units={},std::optional<ActorContext> unitContext={});
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    bool hasOutputCapacity(size_t extraFacts = 0) const { return ports_.events.hasCapacity(2 + extraFacts); }
    DomainResult<Plan> prepare(Change,const PlayerState *candidate=nullptr,const InitialEffectMutation &initialAura={},std::shared_ptr<effects::AuraUnitPublication> units={});
    DomainResult<std::vector<Plan>> prepareGroup(std::vector<Change>,std::shared_ptr<effects::AuraUnitPublication> units={});
    using AuraRecipient=std::function<void(const ActorContext &,const AuraDefinition &,const SkillAuraChange &,EntityId)>;
    using UnitAuraMutation=std::function<void(const AuraRecipient &)>;
    DomainResult<std::vector<Plan>> prepareUnitAuras(std::shared_ptr<effects::AuraUnitPublication>,const UnitAuraMutation &);
    DomainResult<> commitUnitAuras(std::vector<Plan>,std::shared_ptr<effects::AuraUnitPublication>,const ActorContext &);
    DomainResult<> initializeItemAuras(PlayerId,uint64_t tick);
    // Native draws publish without manufacturing a client attribute change.
    DomainResult<> consumeRandom(const ActorContext &,uint64_t expected,uint64_t next);
    DomainResult<> consumeRoomRandom(const RoomRandomLoan &);
    DomainResult<> consumeRoomRandom(std::span<const std::shared_ptr<const RoomRandomLoan>>,std::optional<EventBatch> facts={});
    DomainResult<> commit(Plan);
    // Character-only skill groups, or up to eight same-area wallets coupled to
    // one ground-item edit. Reserve every projection before committing any owner.
    DomainResult<> commitCharacters(std::vector<Plan>, std::optional<WorldEdit> = {});
    // Two inventory owners, one output reservation and no intermediate writes.
    DomainResult<> commitInventories(std::vector<Plan>);
    // Atomic mana debit and optional relocation through connected same-act areas.
    DomainResult<> release(const ActorContext &, uint64_t expectedCharacterRevision, Fixed8 manaCost,
                           std::optional<PointTarget> relocation = {}, std::optional<SkillCharge> charge = {},
                           std::vector<DomainFact> publicFacts = {}, std::optional<UnitRandomEdit> random = {},int skill=-1);
    // Resource-only commit: no inventory clone or equipment re-evaluation per hit.
    DomainResult<> resources(const ActorContext &, uint64_t expected, Fixed8 life, Fixed8 mana, Fixed8 stamina,
                             std::vector<DomainFact> publicFacts = {});
    DomainResult<> damage(const ActorContext &, uint64_t expectedCharacterRevision, int64_t amount);
};
}
