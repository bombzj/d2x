#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "server/area_store.hpp"
#include "gameplay/loot/request.hpp"
#include "gameplay/loot/plan.hpp"
#include "gameplay/loot/chest.hpp"
#include "gameplay/maps/pd2/loot_program.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/transactions/system.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::loot {
// Treasure selection only; a selected plan is not materialized or committed loot.
struct ObjectSource { int definition{}, operation{}; std::optional<ChestState> chest; ItemHandle key; std::string dropCode{}; int dropCount{},dropLevel{}; ItemHandle ingredient{}; bool reportCompletion{true}; };
struct NativeDrop {
    enum class UniqueSelection { Exact,Base };
    enum class Adjustment { None,Corruption,RandomSockets };
    std::string code;int level{99};ItemQuality quality{ItemQuality::Unique};
    std::optional<size_t> uniqueRow;
    UniqueSelection uniqueSelection{};
    bool resetUnique{},crtInitialization{};
    Adjustment adjustment{};
    bool exactPosition{},placementValid{true};
    int preferredPrefix{},preferredSuffix{};
    bool operator==(const NativeDrop &) const = default;
};
struct NativeTreasure {std::string name;int level{};bool operator==(const NativeTreasure &) const=default;};
struct Completion {
    bool accepted{};size_t created{};
    Completion(bool result=false,size_t count=0):accepted(result),created(count) {}
};
struct Request { uint64_t occurrence{}; LootRequest source; PlayerId beneficiary; Vec position; std::optional<ObjectSource> object{}; std::optional<CharacterRecord> questClaimant{}; bool towerGold{},environment{}; int additionalMagicFind{},additionalGoldFind{}; bool reportCompletion{}; std::vector<NativeDrop> nativeDrops{}; std::optional<NativeTreasure> nativeTreasure{}; };
struct Preparation { Request request; PersistentCharacter character; std::string classCode; uint64_t seed{}; int magicFind{}, goldFind{}; std::set<size_t> uniques; unsigned effectivePlayers=1; std::vector<ItemSeeds> itemSeeds{}; std::optional<items::PreparedBatch> prepared{}; std::optional<uint64_t> monsterSeed{},objectSeed{}; std::optional<RoomRandomLoan> roomSeed{}; uint64_t order{}; std::shared_ptr<const pd2::MapInstance> pd2Map{}; std::shared_ptr<const pd2::MapCatalog> pd2Loot{}; std::optional<pd2::MapMonsterLoot> pd2Monster{}; uint32_t pd2Random{}; };
struct State { std::map<EntityId, Preparation> pending; std::set<size_t> uniques; std::string deferred; std::map<EntityId, Completion> completed; uint64_t nextOrder{1}; };
struct Ports { const PlayerStore &players; const quests::System &quests; items::System &items; transactions::System &transactions; uint64_t &random; const TreasureRules *definitions; const GameSettings &settings; EventOutbox &events; const social::System &social; monsters::System &monsters; objects::System &objects; AreaStore &areas; const spatial::System &spatial; uint32_t &pd2Random; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    std::set<size_t> prepareUniques(const std::set<size_t> &) const;
    void commitUniques(std::set<size_t> next) noexcept { state_.uniques.swap(next); }
    DomainResult<> queue(const Request &, std::optional<transactions::CharacterEdit> payment = {});
    std::optional<ItemSeeds> reserveSeeds(EntityId, uint64_t occurrence, size_t index);
    std::optional<Preparation> beginPreparation(EntityId);
    std::optional<bool> takeCompletion(EntityId);
    std::optional<Completion> takeCompletionDetail(EntityId);
    DomainResult<> install(EntityId source, TickContext, items::PreparedBatch, std::string deferred,
      std::optional<uint64_t> sourceRandom = {},std::optional<uint64_t> objectRandom = {},std::optional<uint32_t> pd2Random = {});
    StepStatus step(TickContext, FrameFacts &);
};
}
