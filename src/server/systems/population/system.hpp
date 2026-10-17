#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/monsters/region_generation.hpp"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::population {
// Prepared spawn identities and per-area population lifecycle; no implicit Fallen fallback.
struct DensityRoom {
  enum class Phase { Trial,Search,Leader,LeaderSetup,PartySetup,Party,Hut,Group,Modifiers,Champions,ChampionModifiers,Extras,Done };
  Phase phase{Phase::Trial};
  size_t logical{};
  int trial{},search{},definition{-1},remaining{},partyRemaining{},partyIndex{},champions{-1};
  uint64_t occurrence{};
  bool initialized{},elite{},champion{},groupCounted{};
  bool championParty{};
  bool retried{};
  bool finalRetried{};
  bool searchPending{};
  bool spawned{},counted{};
  std::string deferred;
  int partyOffset{};
  size_t extra{};
  int extraRemaining{-1},partyDefinition{-1};
  bool extraParty{};
  Vec position;
  EntityId leader,partyOwner,championChild;
  std::vector<int> modifiers;
  std::vector<EntityId> minions;
};
struct Coverage {
    size_t pendingRooms{},deferred{};
    bool complete() const {return !pendingRooms && !deferred;}
};
struct Neighborhood {Vec center;float radius;};
struct AreaPopulation { uint64_t generation{}; uint64_t validatedGeneration{}; std::map<size_t,uint8_t> roomStages; std::set<EntityId> admittedNpcs; std::map<size_t,DensityRoom> density,presets; int processedRooms{},successfulRooms{},uniqueCount{}; std::set<EntityId> countedUniques; };
struct QuestBirth {RegionId area;uint64_t generation{};std::string name;DensityRoom cursor;std::optional<Vec> position;};
struct ObjectBirth {RegionId area;uint64_t generation{};EntityId source;Vec position;int definition{-1};DensityRoom cursor;bool elite{true};unsigned occurrence{};int retryAttempts{},finalRetryAttempts{},birthTicks{};};
struct WanderRoom {
  enum class Phase {Check,Search,Birth,Done};
  uint64_t epoch{};
  Phase phase{Phase::Check};
  int search{};
  Vec position;
  std::string deferred;
};
struct State { std::map<RegionId, AreaPopulation> areas; std::optional<uint64_t> regionRandom; std::map<int,MonsterRegionSelection> regions; std::map<int,std::vector<std::string>> rosters; std::set<int> superUniques; std::vector<QuestBirth> questBirths; std::vector<ObjectBirth> objectBirths; std::map<std::pair<RegionId,size_t>,WanderRoom> wanderRooms; std::map<RegionId,int> wanderCounts; std::array<uint64_t,5> roomEpochs{}; };
struct Ports { AreaStore &areas; monsters::System &monsters; uint64_t &random; const GameSettings &settings; objects::System &objects; const spatial::System &spatial; quests::System &quests; const PlayerStore &players; world::System &world; };
class System {
    State state_;
    const Ports ports_;
    StepStatus populateDensity(RegionId,size_t,TickContext,std::optional<size_t> preset={},QuestBirth *quest=nullptr,ObjectBirth *object=nullptr);
    StepStatus populateWanderer(RegionId,size_t,uint64_t,TickContext);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    void initializeRegions(const MonsterRegionRules &);
    const State &read() const { return state_; }
    Coverage coverage(RegionId,std::optional<Neighborhood> neighborhood={}) const;
    DomainResult<> requestQuestBirth(RegionId,std::string_view,TickContext,std::optional<Vec> position={});
    DomainResult<> requestObjectBirth(RegionId,EntityId,Vec,TickContext,std::optional<int> definition={},unsigned occurrence=0,
      int retryAttempts=0,int finalRetryAttempts=0,int birthTicks=0);
    StepStatus advanceQuestBirths(TickContext);
    bool questBirthComplete(RegionId,std::string_view) const;
    void resetQuestBirth(RegionId,std::string_view);
    StepStatus step(TickContext, FrameFacts &);
};
}
