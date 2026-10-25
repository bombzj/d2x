#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "preparation.hpp"
#include "server/systems/actions/requests.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::monsters { struct Actor; }
namespace d2x::server { struct RoomRandomLoan; }

namespace d2x::server::companions {
// Ownership and control for hirelings/summons; live actors belong to Monsters.
enum class Kind { Hireling, Summon, IronGolem };
enum class Action { Hire, Resurrect, Dismiss, List, Equipment, Potion };
struct Request { Action action; EntityId npc; std::optional<uint32_t> offer; std::optional<ItemHandle> item{}; bool belt{}; };
struct ShadowDecision;struct DruidDecision;
struct Companion { EntityId actor; PlayerId owner; Kind kind; std::optional<uint16_t> sourceSkill;
    int rank{}, attackSkill{}; uint64_t expires{}, nextDecision{}, release{}, removeAt{}; EntityId target{};
    uint64_t ownerTeleport{}; int attackBias{},projectedSkillBonus{};
    int ownerLevel{};std::map<int,int> effectiveRanks{},hardRanks{};
    std::array<int64_t,3> assassinAi{-1,-1,0};
    std::array<int64_t,3> druidAi{-1,0,0};
    Vec assassinOrigin{},assassinTarget{};
    EntityId assassinMissile{},lastCorpse{};
    bool shadowInitialized{},nativePetMoving{};
    std::shared_ptr<ShadowDecision> shadowDecision{};
    std::shared_ptr<DruidDecision> druidDecision{};
    std::optional<actions::MonsterCastRequest> pendingAction{};
    std::optional<AuraDefinition> pendingAura{};
    uint64_t observedAiThinkRevision{};
};
struct State { std::map<EntityId, Companion> companions; };
struct Ports { const PlayerStore &players; monsters::System &monsters; skills::System &skills; transactions::System &transactions; missiles::System &missiles; const AreaStore &areas; EventOutbox &events; uint64_t &random; effects::System &effects; npc::System &npc; inventory::System &inventory; combat::System &combat; const social::System &social; const spatial::System &spatial; ai::System &ai; items::System &items; };
class System {
    State state_;
    const Ports ports_;
    std::map<EntityId,Preparation> pending_;
    std::map<EntityId,Prepared> prepared_;
    uint64_t nextPreparation_{1};
    std::set<EntityId> materialRestorations_;
    void synchronizeMaterialRestoration(uint64_t tick);
    StepStatus synchronizeIronGolems(TickContext);
    DomainResult<> revive(const ActorContext &,const SkillCastSpec &,EntityId);
    std::map<PlayerId,PreparedHireling> hirelingRules_;
    struct HirelingReplacement {
        HirelingPreparation source;
        uint64_t characterRevision{};
        std::optional<Request> request;
        std::optional<PreparedHireling> prepared;
    };
    std::map<PlayerId,HirelingReplacement> hirelingReplacements_;
    uint64_t nextHirelingReplacement_{1};
    struct HirelingBirthPublication;
    DomainResult<std::shared_ptr<HirelingBirthPublication>> prepareHirelingBirth(const ActorContext &,
        const PersistentCharacter &,const PreparedHireling &,std::optional<Vec> seller,bool replacement);
    DomainResult<bool> replaceHireling(const ActorContext &,HirelingRecord,unsigned price,std::optional<Request> request={},std::optional<Vec> seller={});
    std::map<PlayerId,HirelingListPreparation> pendingLists_;
    std::map<PlayerId,PreparedHirelingList> hirelingLists_;
    uint64_t nextList_ = 1;
    StepStatus amazonStep(Companion &,TickContext);
    StepStatus assassinStep(Companion &,TickContext);
    StepStatus refreshDruid(Companion &,TickContext);
    StepStatus druidStep(Companion &,TickContext);
    DomainResult<> nativeSummon(const ActorContext &,const SkillCastSpec &,Vec);
    StepStatus shadowStep(Companion &,TickContext);
    StepStatus necroStep(Companion &,TickContext);
    StepStatus refreshNecro(Companion &,TickContext);
    StepStatus hirelingStep(Companion &,TickContext);
    DomainResult<bool> maintainPd2Hireling(Companion &,const PreparedHireling &,TickContext);
    StepStatus advancePet(EntityId,TickContext);
    DomainResult<> requestAction(Companion &,const actions::MonsterCastRequest &);
    StepStatus synchronizeHirelings(TickContext);
    DomainResult<> equipHireling(const ActorContext &, uint32_t body);
    DomainResult<> drink(const ActorContext &,ItemHandle,bool belt);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    std::optional<DomainResult<>> restoreMaterial(EntityId item,uint64_t tick);
    std::string hirelingDeferred(PlayerId player) const {
        const auto found=hirelingRules_.find(player);return found==hirelingRules_.end()?std::string{}:found->second.deferred;
    }
    std::vector<Preparation> pending() const;
    DomainResult<> install(Prepared);
    std::vector<HirelingPreparation> pendingHirelings(uint64_t tick) const;
    DomainResult<> install(PreparedHireling);
    std::vector<HirelingListPreparation> pendingHirelingLists() const;
    DomainResult<> install(PreparedHirelingList);
    void cancel(EntityId);
    DomainResult<> amazon(const ActorContext &, const SkillCastSpec &, Vec);
    DomainResult<> assassin(const ActorContext &, const SkillCastSpec &, Vec);
    DomainResult<> druid(const ActorContext &,const SkillCastSpec &,Vec);
    DomainResult<> necro(const ActorContext &, const SkillCastSpec &, Vec,EntityId,bool restoring=false);
    DomainResult<bool> growBoneWall(EntityId owner,EntityId root,int skill,int rank,RegionId,Vec,uint64_t tick);
    DomainResult<> hydra(const ActorContext &, const SkillCastSpec &, Vec);
    DomainResult<> execute(const ActorContext &, const Request &);
    DomainResult<> grantHireling(const ActorContext &,HirelingRecord);
    bool canDismiss(const ActorContext &, EntityId) const;
    DomainResult<> dismiss(const ActorContext &, EntityId);
    DomainResult<> ownerDied(const ActorContext &);
    std::optional<HirelingExperienceAward> experience(PlayerId,const monsters::Actor &) const;
    DomainResult<> awardExperience(const ActorContext &,const HirelingExperienceAward &);
    StepStatus step(TickContext, FrameFacts &);
    DomainResult<> think(EntityId,TickContext);
};
}
