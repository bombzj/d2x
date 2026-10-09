#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "preparation.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::companions {
// Ownership and control for hirelings/summons; live actors belong to Monsters.
enum class Kind { Hireling, Summon, IronGolem };
enum class Action { Hire, Resurrect, Dismiss };
struct Request { Action action; EntityId npc; std::optional<uint32_t> offer; };
struct Summon { PlayerId owner; Kind kind; monsters::Admission actor; std::optional<uint16_t> sourceSkill; };
struct Companion { EntityId actor; PlayerId owner; Kind kind; std::optional<uint16_t> sourceSkill;
    int rank{}, attackSkill{}; uint64_t ownerDeath{}; uint64_t expires{}, nextDecision{}, release{}, removeAt{}; EntityId target{}; uint64_t random{};
};
struct State { std::map<EntityId, Companion> companions; };
struct Ports { const PlayerStore &players; monsters::System &monsters; skills::System &skills; transactions::System &transactions; missiles::System &missiles; const AreaStore &areas; EventOutbox &events; uint64_t &random; effects::System &effects; };
class System {
    State state_;
    const Ports ports_;
    std::map<EntityId,Preparation> pending_;
    std::map<EntityId,Prepared> prepared_;
    std::map<PlayerId,PreparedHireling> hirelingRules_;
    StepStatus amazonStep(Companion &,TickContext);
    StepStatus hirelingStep(Companion &,TickContext);
    StepStatus synchronizeHirelings(TickContext);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    std::string hirelingDeferred(PlayerId player) const {
        const auto found=hirelingRules_.find(player);return found==hirelingRules_.end()?std::string{}:found->second.deferred;
    }
    DomainResult<EntityId> summon(const Summon &);
    std::vector<Preparation> pending() const;
    DomainResult<> install(Prepared);
    std::vector<HirelingPreparation> pendingHirelings() const;
    DomainResult<> install(PreparedHireling);
    void cancel(EntityId);
    DomainResult<> amazon(const ActorContext &, const SkillCastSpec &, Vec);
    DomainResult<> hydra(const ActorContext &, const SkillCastSpec &, Vec);
    DomainResult<> execute(const ActorContext &, const Request &);
    bool canDismiss(const ActorContext &, EntityId) const;
    DomainResult<> dismiss(const ActorContext &, EntityId);
    StepStatus step(TickContext, FrameFacts &);
};
}
