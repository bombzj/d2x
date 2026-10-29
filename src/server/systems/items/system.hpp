#pragma once
#include "core/id.hpp"
#include "state.hpp"
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/items/generation.hpp"
#include "gameplay/items/replenishment.hpp"
#include "gameplay/items/native_save.hpp"
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
struct PreparedBatch {
    std::vector<ItemInstance> items;
    std::shared_ptr<const EquipmentRules> equipment;
    std::set<size_t> limitedUniques;
    std::map<EntityId,EntityId> identities{}; // Prepared item ID -> already allocated native unit.
};
struct Address { std::optional<PlayerId> character; ItemHandle item; };
struct InactiveItem {
    Bytes bitstream;
    struct Derived {int requiredLevel{},socketRequiredLevel{};};
    std::vector<Derived> derived;
    EquipmentValues equipment;
    uint64_t expires{};
    std::optional<PlayerId> goldOwner;
};
using InactiveRooms=std::map<std::pair<RegionId,size_t>,std::vector<InactiveItem>>;
struct InactiveItemRequest {uint64_t token{};ItemInstance item;Vec global;};
struct PreparedInactiveItem {InactiveItemRequest source;InactiveItem node;std::string deferred;};
struct RoomCompression {
    uint64_t expectedRevision{};
    State next;
    InactiveRooms rooms;
    std::vector<EntityId> retired;
};
struct Ports { const PlayerStore &players; AreaStore &areas; EntityIds &ids; uint64_t &random; const ItemCatalog *definitions; EventOutbox &events; FrameEventQueue &frameEvents; const spatial::System &spatial; std::shared_ptr<const NativeItemSaveRules> itemSave; };
class System {
    friend class transactions::System;
    friend class loot::System;
    struct GroundLifetime { uint64_t generation{}, expires{}; };
    std::map<EntityId,GroundLifetime> groundLifetimes_;
    std::map<EntityId,ItemRestorationClock> restoration_;
    InactiveRooms inactive_;
    std::string inactiveFailure_;
    struct InactivePreparation {InactiveItemRequest source;std::optional<PreparedInactiveItem> prepared;};
    std::map<EntityId,InactivePreparation> inactivePreparation_;
    uint64_t nextInactiveToken_{1};
    void commit(State next) noexcept;
    void prepareGroundRooms(State &) const;
    State state_;
    mutable uint64_t groundIndexRevision_{};
    mutable std::map<RegionId,std::vector<EntityId>> groundByArea_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    const InactiveRooms &inactiveRooms() const {return inactive_;}
    void prepareGroundOrder(State &,std::span<const DomainFact>) const;
    DomainResult<RoomCompression> prepareRoomCompression(RegionId,size_t,uint64_t tick,EntityId only={});
    const std::string &inactiveFailure() const {return inactiveFailure_;}
    std::optional<InactiveItemRequest> pendingInactiveItem();
    DomainResult<> prepareInactiveItem(PreparedInactiveItem);
    void publishRoomCompression(RoomCompression) noexcept;
    void retireCompressedItem(const RoomCompression &,EntityId) noexcept;
    StepStatus restoreRoom(RegionId,size_t,uint64_t tick);
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
        auto [it,created]=restoration_.try_emplace(id,ItemRestorationClock{itemRestorationDue(tick,rule,true),rule.rate,rule.quantity,rule.charges,rule.halfInterval});
        if(!created && (it->second.rate!=rule.rate || it->second.quantity!=rule.quantity || it->second.charges!=rule.charges || it->second.halfInterval!=rule.halfInterval))
            it->second={itemRestorationDue(tick,rule,true),rule.rate,rule.quantity,rule.charges,rule.halfInterval};
        if(!ports_.frameEvents.hasEvent({FrameEventUnit::Item,id},FrameEventType::StatRegeneration))
            ports_.frameEvents.insertPrepared({{},{FrameEventUnit::Item,id},FrameEventType::StatRegeneration,it->second.due});
        return it->second;
    }
    void rearmRestoration(EntityId id,ItemRestoration rule,uint64_t tick) {
        const auto due=itemRestorationDue(tick,rule,false);
        auto timer=ports_.frameEvents.prepareInsertion({{},{FrameEventUnit::Item,id},FrameEventType::StatRegeneration,due});
        restoration_.insert_or_assign(id,ItemRestorationClock{due,rule.rate,rule.quantity,rule.charges,rule.halfInterval});
        ports_.frameEvents.cancelHandler({FrameEventUnit::Item,id},FrameEventType::StatRegeneration,0);
        ports_.frameEvents.publish(std::move(timer));
    }
    void clearRestoration(EntityId id) {restoration_.erase(id);ports_.frameEvents.cancelHandler({FrameEventUnit::Item,id},FrameEventType::StatRegeneration,0);}
    DomainResult<> restoreGround(EntityId,uint64_t tick);
    bool identityCapacity(size_t count) const { return !count || (ports_.ids.cursor()<=UINT32_MAX && count<=uint64_t(UINT32_MAX)-ports_.ids.cursor()+1); }
    uint64_t identityCursor() const {return ports_.ids.cursor();}
    std::shared_ptr<EntityIds::Publication> prepareIdentities(uint64_t expected,size_t count) const {
        if(ports_.ids.cursor()!=expected || !identityCapacity(count)) throw std::logic_error("Stale native item identity allocation");
        auto publication=ports_.ids.preparePublication(count);
        for(size_t index=0;index<count;++index) publication->bind(EntityId{expected+index},NativeUnitType::Item);
        return publication;
    }
    void publishIdentities(EntityIds::Publication &publication) noexcept {ports_.ids.publish(publication);}
    void publishIdentities(uint64_t expected,size_t count) {
        auto publication=prepareIdentities(expected,count);publishIdentities(*publication);
    }
    EntityId reserveIdentity(std::optional<NativeUnitType> type=NativeUnitType::Item) {
        if(ports_.ids.cursor()>UINT32_MAX) throw std::overflow_error("Native item identity exhausted");
        return type?ports_.ids.allocate(*type):ports_.ids.allocate();
    }
    DomainResult<State> prepare(PreparedBatch, GroundLocation);
    DomainResult<> install(PreparedBatch, GroundLocation);
    DomainResult<> installNativeGroundBatch(const std::vector<std::pair<PreparedBatch,GroundLocation>> &,uint64_t tick,std::span<const EntityId> identities={},std::shared_ptr<EntityIds::Publication> allocation={},bool flip=true,std::optional<PlayerId> goldOwner={});
    DomainResult<PreparedBatch> prepareNativeGold(unsigned level,uint64_t &random,bool inStore) const;
    struct PlayerGoldSelection {std::string code;unsigned level{};};
    DomainResult<PlayerGoldSelection> selectPlayerGold(RegionId,Vec);
    // Raw CreateItemEx spawn type 3: placement has already been resolved by
    // the caller. Uses the unit seed for quantity, the item seed for quality.
    DomainResult<EntityId> createNativeGold(GroundLocation,unsigned level,uint64_t tick);
    bool reachable(GroundLocation, Vec from) const;
    // Player drops use GetFreeSpaceEx, including its offset and near-room search.
    std::optional<GroundLocation> placement(GroundLocation, const InventoryState &) const;
    DomainResult<ItemInstance> resolve(const Address &) const;
    StepStatus step(TickContext,FrameFacts &);
};
}
