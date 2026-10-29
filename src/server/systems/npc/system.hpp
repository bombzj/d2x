#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/area_store.hpp"
#include "gameplay/npc/intents.hpp"
#include <deque>
#include <span>
#include <utility>
namespace d2x::server::npc {
using Intent = std::variant<TalkToNpc, EndNpcConversation,ConfirmNpcVoice>;
struct Request { Intent intent; };
struct Conversation { EntityId npc; RegionId area{}; uint64_t revision{}; std::map<uint16_t,QuestId> pendingQuests; EntityId actor{}; uint64_t areaGeneration{}; std::optional<uint16_t> pendingIntroduction{}; bool refresh{}; };
struct ServiceAccess { const AreaNpc *npc; const Conversation *conversation; };
struct Escape {RegionId area;Vec destination;std::deque<Vec> route;bool escaped{};};
struct HirelingSlot {uint16_t name{};uint32_t seed{};bool available{},hired{};};
struct HirelingRoster {uint64_t revision{1};std::vector<HirelingSlot> slots;};
struct VendorControlPublication {uint64_t expected{},next{};std::map<int,HirelingRoster> hirelings;};
struct State { std::map<PlayerId, Conversation> conversations; uint64_t next = 1; std::map<EntityId,Escape> escapes; std::optional<uint64_t> controlRandom; std::map<int,HirelingRoster> hirelings; };
struct Ports { const PlayerStore &players; const AreaStore &areas; transactions::System &transactions; EventOutbox &events; const GameSettings &settings; quests::System &quests; effects::System &effects; world::System &world; MovementSystem &movement; const monsters::System &monsters; };
class System {
    State state_; const Ports ports_;
    struct Approach { ActorContext actor; Request request; uint64_t locomotion; };
    std::map<PlayerId,Approach> approaches_;
    Vec position(const ActorContext &, const AreaNpc &) const;
    std::pair<Conversation,NpcMessagesFact> offer(const ActorContext &,const AreaNpc &,const CharacterRecord &) const;
    HirelingRoster prepareHirelings(std::span<const uint16_t>,uint64_t revision,uint64_t &random) const;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    void initializeControl(uint64_t &gameRandom);
    DomainResult<VendorControlPublication> prepareVendorControl(uint64_t expected,uint64_t next,const NpcRule &) const;
    void publishVendorControl(VendorControlPublication);
    DomainResult<HirelingRoster> hirelingRoster(const NpcRule &);
    bool hirelingAvailable(int seller,uint64_t revision,uint16_t name,uint32_t seed) const;
    DomainResult<VendorControlPublication> prepareHirelingHired(int seller,uint64_t revision,uint16_t name,uint32_t seed) const;
    const State &read() const { return state_; }
    const AreaNpc *find(const ActorContext &, EntityId, bool conversation = false) const;
    const Conversation *conversation(PlayerId) const;
    std::optional<ServiceAccess> service(const ActorContext &, EntityId) const;
    DomainResult<> execute(const ActorContext &, const Request &);
    DomainResult<> close(PlayerId);
    DomainResult<> escape(RegionId,EntityId,Vec);
    DomainResult<> finishEscape(EntityId);
    StepStatus step(TickContext, FrameFacts &);
};
}
