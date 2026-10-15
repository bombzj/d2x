#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include "server/systems/monsters/admission.hpp"
#include "gameplay/monsters/region_generation.hpp"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::population {
// Prepared spawn identities and per-area population lifecycle; no implicit Fallen fallback.
using Spawn = monsters::Admission;
struct DensityRoom {
  enum class Phase { Trial,Search,Leader,LeaderSetup,PartySetup,Party,Hut,Group,Modifiers,Champions,ChampionModifiers,Extras,Done };
  Phase phase{Phase::Trial};
  size_t logical{};
  int trial{},search{},definition{-1},remaining{},partyRemaining{},partyIndex{},champions{-1};
  uint64_t occurrence{};
  bool initialized{},elite{},champion{},groupCounted{};
  bool championParty{};
  bool retried{};
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
struct AreaPopulation { uint64_t generation{}; std::set<std::string> admittedSpawnKeys; uint64_t validatedGeneration{}; std::map<size_t,uint8_t> roomStages; std::set<EntityId> admittedNpcs; std::set<std::string> rejectedDensityKeys; std::map<size_t,DensityRoom> density,presets; int processedRooms{},successfulRooms{},uniqueCount{}; std::set<EntityId> countedUniques; };
struct State { std::map<RegionId, AreaPopulation> areas; std::optional<uint64_t> regionRandom; std::map<int,MonsterRegionSelection> regions; std::map<int,std::vector<std::string>> rosters; std::set<int> superUniques; };
struct Ports { AreaStore &areas; monsters::System &monsters; uint64_t &random; const GameSettings &settings; objects::System &objects; const spatial::System &spatial; const quests::System &quests; };
class System {
    State state_;
    const Ports ports_;
    StepStatus populateDensity(RegionId,size_t,TickContext,std::optional<size_t> preset={});
  public:
    explicit System(Ports ports) : ports_(ports) {}
    void initializeRegions(const MonsterRegionRules &);
    const State &read() const { return state_; }
    DomainResult<EntityId> admit(const Spawn &);
    StepStatus step(TickContext, FrameFacts &);
};
}
