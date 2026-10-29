#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/player/corpse.hpp"
#include "server/systems/companions/preparation.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server { struct PlayerState; }
namespace d2x::server::death {
// Death occurrences, resurrection and corpse recovery; no duplicated persistent corpse items.
enum class Action { Resurrect, RecoverCorpse };
struct Request { Action action; std::optional<UnitTarget> corpse; };
struct Transition {
    uint64_t occurrence{}; EntityId victim; bool finalized{}; uint64_t ready{}; bool reviving{}; bool companionsSettled{};
    PlayerMessageFact message; std::optional<PointTarget> revival{}; bool penaltySettled{};uint64_t recoverableExperience{};
    struct EarDrop {GroundLocation location;ItemEarIdentity identity;bool named{},queued{};};
    std::optional<EarDrop> ear;
    struct GoldDrop {
        unsigned amount{},level{};std::string code;
        std::optional<GroundLocation> location;bool selected{},queued{},finished{},created{};
    };
    std::optional<GoldDrop> penaltyGold,corpseGold;
    EntityId corpseId,corpseItems;
};
struct ExperienceShare {PlayerId player;EntityId actor;uint64_t amount{};};
enum class RewardPhase { Restore, HirelingExperience, Loot, PlayerExperience };
struct Reward { PlayerId player; EntityId actor; uint64_t amount{}; int life{},mana{}; CharacterRecord lootClaimant; std::optional<companions::HirelingExperienceAward> hireling{}; std::vector<ExperienceShare> shares{};size_t awarded{}; RewardPhase phase = RewardPhase::Restore; };
struct Recovery { ActorContext actor; EntityId corpse; uint64_t locomotion{}; };
struct State { std::map<EntityId, Transition> transitions; std::map<EntityId, Reward> rewards; std::map<PlayerId, Recovery> recoveries; };
struct Ports { const PlayerStore &players; monsters::System &monsters; progression::System &progression; skills::System &skills; loot::System &loot; items::System &items; transactions::System &transactions; world::System &world; MovementSystem &movement; const AreaStore &areas; const GameSettings &settings; companions::System &companions; const social::System &social; const effects::System &effects; maps::System &maps; };
class System {
    State state_;
    const Ports ports_;
  StepStatus advanceRewards(TickContext);
  const PlayerState *corpseOwner(const ActorContext &, EntityId) const;
    DomainResult<> begin(const ActorContext &);
    DomainResult<> dropEar(const ActorContext &);
    DomainResult<unsigned> dropGold(const ActorContext &,unsigned,bool corpse,const InventoryState *ground=nullptr);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
    DomainResult<> settle(const ActorContext &);
    DomainResult<> recover(const ActorContext &, EntityId);
    void advanceRecovery(TickContext);
};
}
