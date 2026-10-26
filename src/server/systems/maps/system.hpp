#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include <map>
#include <array>
#include <vector>

namespace d2x::server::monsters {struct Actor;}
namespace d2x::server::maps {
struct Death {
    enum class Phase { Counter,Portal,Reward,Wait,Cleanup,Complete } phase{};
    uint64_t occurrence{};
    std::optional<Vec> portalPosition;
    std::optional<size_t> reward;
    bool rewardPositionPrepared{},rewardContext{};
    std::optional<PointTarget> rewardPosition;
    int remaining{-1};bool suppressTreasure{},cleanupPrepared{},evaluated{};
    std::vector<EntityId> cleanup;size_t cleanupIndex{};
    EntityId portalObject{};
};
struct State {
    std::shared_ptr<const pd2::MapInstance> instance;
    std::map<EntityId,Death> deaths;
    struct Binding {EntityId id;RegionId area;Vec position;bool object{};};
    std::array<Binding,8> bindings{};
    uint8_t phase{};
    int32_t pauseDepth{};
    struct PortalBirth {
        pd2::MapPortalProgram program;
        std::optional<PointTarget> landing{};EntityId destination{};
        int phase{};
    };
    std::map<EntityId,PortalBirth> portals;
};
struct Ports {
    const PlayerStore &players;const AreaStore &areas;monsters::System &monsters;
    travel::System &travel;world::System &world;loot::System &loot;
    transactions::System &transactions;uint32_t &random;const spatial::System &spatial;
    objects::System &objects;
    EventOutbox &events;
};
class System {
    State state_;
    const Ports ports_;
    bool handles(const monsters::Actor &) const;
    DomainResult<> advanceDeath(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceRandomRewards(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceCharmReward(const monsters::Actor &,Death &,TickContext);
    DomainResult<> advanceImperialReward(const monsters::Actor &,Death &,TickContext);
    void prepareDropPosition(const monsters::Actor &,Death &,bool requireSearch=false) const;
    void synchronize();
    bool advancePortals(TickContext);
public:
    explicit System(Ports ports):ports_(ports) {}
    const State &read() const {return state_;}
    void bind(int slot,EntityId,RegionId,Vec,bool object);
    DomainResult<> operateTome(const ActorContext &,EntityId);
    void objectRestored(EntityId previous,EntityId current);
    bool opensBlockerOnBirth(int definition) const {
        return state_.instance && state_.instance->controller==9 && state_.phase==2 && definition>=616 && definition<=619;
    }
    bool deathReady(const monsters::Actor &) const;
    bool skipsTreasure(const monsters::Actor &) const;
    StepStatus step(TickContext,FrameFacts &);
};
}
