#pragma once
#include "state.hpp"
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/items/generation.hpp"
#include "gameplay/items/replenishment.hpp"
#include "gameplay/events/frame_queue.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <span>

namespace d2x::server::items {
// Canonical non-character items; character-owned items remain in PlayerStore.
struct CreateItem { std::string code; unsigned level{}; ItemGeneration generation; ItemLocation location; };
struct PreparedBatch { std::vector<ItemInstance> items; std::shared_ptr<const EquipmentRules> equipment; std::set<size_t> limitedUniques; };
struct Address { std::optional<PlayerId> character; ItemHandle item; };
struct Ports { const PlayerStore &players; const AreaStore &areas; EntityIds &ids; uint64_t &random; const ItemCatalog *definitions; EventOutbox &events; FrameEventQueue &frameEvents; };
class System {
    friend class transactions::System;
    friend class loot::System;
    struct GroundLifetime { uint64_t generation{}, expires{}; };
    std::map<EntityId,GroundLifetime> groundLifetimes_;
    std::map<EntityId,ItemRestorationClock> restoration_;
    void commit(State next) noexcept {
        std::erase_if(next.goldOwners,[&](const auto &entry){return !next.world.items.contains(entry.first);});
        std::erase_if(groundLifetimes_,[&](const auto &entry){
            const auto item=next.world.items.find(entry.first);
            return item==next.world.items.end() || item->second.location!=state_.world.items.at(entry.first).location;
        });
        std::swap(state_, next);
    }
    State state_;
    mutable uint64_t groundIndexRevision_{};
    mutable std::map<RegionId,std::vector<EntityId>> groundByArea_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    std::vector<EntityId> groundInAreas(std::span<const RegionId>) const;
    // Stable for this stay on the ground, including regeneration/partial pickup.
    // Removal, pickup followed by drop, or relocation retires the old identity.
    uint64_t groundGeneration(EntityId id) const {
        const auto item=state_.world.items.find(id);
        if(item==state_.world.items.end() || !std::holds_alternative<GroundLocation>(item->second.location)) return 0;
        const auto lifetime=groundLifetimes_.find(id);
        return lifetime==groundLifetimes_.end()?item->second.revision:lifetime->second.generation;
    }
    // One clock per real item identity, retained across drop/pickup transfers.
    ItemRestorationClock &restorationClock(EntityId id,ItemRestoration rule,uint64_t tick) {
        auto [it,created]=restoration_.try_emplace(id,ItemRestorationClock{itemRestorationDue(tick,rule.rate,true),rule.rate,rule.quantity});
        if(!created && (it->second.rate!=rule.rate || it->second.quantity!=rule.quantity)) it->second={itemRestorationDue(tick,rule.rate,true),rule.rate,rule.quantity};
        if(!ports_.frameEvents.hasEvent({FrameEventUnit::Item,id},FrameEventType::StatRegeneration))
            ports_.frameEvents.insertPrepared({{},{FrameEventUnit::Item,id},FrameEventType::StatRegeneration,it->second.due});
        return it->second;
    }
    void rearmRestoration(EntityId id,ItemRestoration rule,uint64_t tick) {
        const auto due=itemRestorationDue(tick,rule.rate,false);
        auto timer=ports_.frameEvents.prepareInsertion({{},{FrameEventUnit::Item,id},FrameEventType::StatRegeneration,due});
        restoration_.insert_or_assign(id,ItemRestorationClock{due,rule.rate,rule.quantity});
        ports_.frameEvents.cancelHandler({FrameEventUnit::Item,id},FrameEventType::StatRegeneration,0);
        ports_.frameEvents.publish(std::move(timer));
    }
    void clearRestoration(EntityId id) {restoration_.erase(id);ports_.frameEvents.cancelHandler({FrameEventUnit::Item,id},FrameEventType::StatRegeneration,0);}
    DomainResult<> restoreGround(EntityId,uint64_t tick);
    bool identityCapacity(size_t count) const { return ports_.ids.cursor()<=UINT32_MAX && count<=uint64_t(UINT32_MAX)-ports_.ids.cursor()+1; }
    EntityId reserveIdentity() { if (ports_.ids.cursor() > UINT32_MAX) throw std::overflow_error("Native item identity exhausted"); return ports_.ids.allocate(); }
    DomainResult<State> prepare(PreparedBatch, GroundLocation);
    DomainResult<> install(PreparedBatch, GroundLocation);
    DomainResult<> installNativeGroundBatch(const std::vector<std::pair<PreparedBatch,GroundLocation>> &,uint64_t tick);
    // Raw CreateItemEx spawn type 3: placement has already been resolved by
    // the caller. Uses the unit seed for quantity, the item seed for quality.
    DomainResult<EntityId> createNativeGold(GroundLocation,unsigned level,uint64_t tick);
    bool reachable(GroundLocation, Vec from) const;
    std::optional<Vec> placement(GroundLocation, const InventoryState &) const;
    DomainResult<ItemInstance> resolve(const Address &) const;
    StepStatus step(TickContext,FrameFacts &);
};
}
