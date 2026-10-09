#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <cmath>
#include <algorithm>
namespace d2x::server::items {
bool System::reachable(GroundLocation target, Vec from) const {
    const auto *area = ports_.areas.find(target.region);
    return area && area->definition.collision.collisionSegment(from, target.position, 0x0801);
}
std::optional<Vec> System::placement(GroundLocation origin, const InventoryState &world) const {
    const auto *area = ports_.areas.find(origin.region);
    if (!area || !std::isfinite(origin.position.x) || !std::isfinite(origin.position.y)) return {};
    for (int radius = 0; radius <= 4; ++radius)
        for (int y = -radius; y <= radius; ++y) for (int x = -radius; x <= radius; ++x) {
            if (std::max(std::abs(x), std::abs(y)) != radius) continue;
            const Vec point{std::floor(origin.position.x) + float(x), std::floor(origin.position.y) + float(y)};
            if (!area->definition.collision.walkable(point, playerMovement)) continue;
            bool occupied = false;
            for (const auto &[id, item] : world.items) {
                (void)id;
                const auto *at = std::get_if<GroundLocation>(&item.location);
                if (at && at->region == origin.region && std::floor(at->position.x) == point.x && std::floor(at->position.y) == point.y) { occupied = true; break; }
            }
            if (!occupied) return point;
        }
    return {};
}
DomainResult<State> System::prepare(PreparedBatch batch, GroundLocation origin) {
    if (!batch.equipment || batch.items.size() > 64 || state_.world.items.size() + batch.items.size() > 4096 || state_.revision == UINT64_MAX)
        return {DomainStatus::Capacity, {}};
    size_t identities=0;
    for(const auto &item:batch.items) {
        identities+=1+item.socketedItems.size();
        if(std::any_of(item.socketedItems.begin(),item.socketedItems.end(),[](const auto &child){return !child.socketedItems.empty();})) return {DomainStatus::InvalidRequest,{}};
    }
    if(!identityCapacity(identities)) return {DomainStatus::Capacity,{}};
    auto next = state_;
    for (auto &item : batch.items) {
        const auto position = placement(origin, next.world);
        if (!position) return {DomainStatus::Conflict, {}};
        auto properties = batch.equipment->items.at(item.id);
        if (ports_.ids.cursor() > UINT32_MAX) return {DomainStatus::Capacity, {}};
        item.id = ports_.ids.allocate(); item.location = GroundLocation{origin.region, *position};item.revision=1;
        for(size_t index=0;index<item.socketedItems.size();++index) {
            auto &child=item.socketedItems[index];child.id=ports_.ids.allocate();child.revision=1;child.location=SocketLocation{item.id,unsigned(index)};
        }
        next.equipment.items.emplace(item.id, std::move(properties));
        next.world.items.emplace(item.id, std::move(item));
    }
    next.equipment.includeSets(*batch.equipment);
    ++next.revision; return {DomainStatus::Applied, std::move(next)};
}
DomainResult<> System::install(PreparedBatch batch, GroundLocation origin) {
    auto next = prepare(std::move(batch), origin); if (!next) return {next.status, {}};
    commit(std::move(*next.value)); return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<ItemInstance> System::resolve(const Address &address) const {
    const InventoryState *inventory = &state_.world;
    if (address.character) {
        const auto *player = ports_.players.find(*address.character);
        if (!player) return {DomainStatus::InvalidActor, {}};
        inventory = &player->persistent.inventory;
    }
    const auto found = inventory->items.find(address.item.id);
    if (found == inventory->items.end() || found->second.revision != address.item.revision) return {DomainStatus::Stale, {}};
    return {DomainStatus::Applied, found->second};
}
StepStatus System::step(TickContext tick,FrameFacts &) {
    bool blocked=false;
    std::set<EntityId> present;
    for(const auto &[id,item]:state_.world.items) present.insert(id);
    for(const auto &[id,player]:ports_.players.all()) for(const auto &[itemId,item]:player.persistent.inventory.items) present.insert(itemId);
    std::erase_if(restoration_,[&](const auto &entry){return !present.contains(entry.first);});
    for(auto it=state_.world.items.begin();it!=state_.world.items.end();) {
        auto &item=it->second;const auto *base=ports_.definitions?ports_.definitions->find(item.definition):nullptr;
        const auto *ground=std::get_if<GroundLocation>(&item.location);
        // Capture the landing identity before restoration can revise properties.
        auto lifetime=groundLifetimes_.end();
        if(base && ground) {
            auto [entry,inserted]=groundLifetimes_.try_emplace(item.id,GroundLifetime{item.revision,0});
            lifetime=entry;
            if(inserted && !base->questTag) {
                // ITEMS_GetGroundRemovalTime: original 25Hz frame constants.
                const bool rare=item.quality==ItemQuality::Rare || item.quality==ItemQuality::Set || item.quality==ItemQuality::Unique || item.quality==ItemQuality::Crafted;
                const uint64_t duration=rare || (base->equipment.isType("gold") && item.quantity>10000)?45000:base->equipment.isType("sock")?30000:15000;
                entry->second.expires=tick.tick>UINT64_MAX-duration?UINT64_MAX:tick.tick+duration;
            }
        }
        if(base && ground && state_.equipment.items.contains(item.id)) {
            const auto &values=state_.equipment.at(item.id,1);
            if(const auto rule=itemRestoration(item,*base,values.stats,values.maximumDurability)) {
                auto &value=rule->quantity?item.quantity:item.durability;
                if(value<rule->maximum) {
                    auto &clock=restorationClock(item.id,*rule,tick.tick);
                    if(clock.due<=tick.tick && item.revision!=UINT64_MAX && state_.revision!=UINT64_MAX) {
                        if(ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},ground->region},{GroundRestoredFact{item.id,item.revision+1}}})) {
                            ++value;++item.revision;++state_.revision;clock.due=itemRestorationDue(tick.tick,rule->rate,false);
                        } else blocked=true;
                    }
                } else clearRestoration(item.id);
            } else clearRestoration(item.id);
        }
        if(!base || !ground || base->questTag) {++it;continue;}
        if(tick.tick<lifetime->second.expires) {++it;continue;}
        if(state_.revision==UINT64_MAX || !ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},ground->region},{GroundRemoveFact{item.id}}})) {blocked=true;++it;continue;}
        groundLifetimes_.erase(item.id);clearRestoration(item.id);state_.equipment.items.erase(item.id);it=state_.world.items.erase(it);++state_.revision;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
