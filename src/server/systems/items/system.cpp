#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/spatial/system.hpp"
#include "core/random.hpp"
#include <cmath>
#include <algorithm>
#include <tuple>
namespace d2x::server::items {
namespace {
using GroundOccupancy = std::set<std::pair<int,int>>;
GroundOccupancy groundOccupancy(const InventoryState &world, RegionId region) {
    GroundOccupancy result;
    for (const auto &[id,item] : world.items) {
        (void)id;
        if (const auto *at=std::get_if<GroundLocation>(&item.location); at && at->region==region)
            result.emplace(int(std::floor(at->position.x)),int(std::floor(at->position.y)));
    }
    return result;
}
std::optional<Vec> groundPlacement(const Grid &grid, Vec origin, const GroundOccupancy &occupied) {
    for (int radius=0;radius<=4;++radius)
        for (int y=-radius;y<=radius;++y) for (int x=-radius;x<=radius;++x) {
            if (std::max(std::abs(x),std::abs(y))!=radius) continue;
            const Vec point{std::floor(origin.x)+float(x),std::floor(origin.y)+float(y)};
            if (grid.walkable(point,playerMovement) && !occupied.contains({int(point.x),int(point.y)})) return point;
        }
    return {};
}
}
std::vector<EntityId> System::groundInAreas(std::span<const RegionId> areas) const {
    if (groundIndexRevision_!=state_.revision) {
        std::map<RegionId,std::vector<EntityId>> next;
        for (const auto &[id,item] : state_.world.items)
            if (const auto *at=std::get_if<GroundLocation>(&item.location)) next[at->region].push_back(id);
        groundByArea_.swap(next);groundIndexRevision_=state_.revision;
    }
    std::vector<EntityId> result;
    for (const auto area:areas) if (const auto found=groundByArea_.find(area);found!=groundByArea_.end())
        result.insert(result.end(),found->second.begin(),found->second.end());
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}
bool System::reachable(GroundLocation target, Vec from) const {
    const auto *area = ports_.areas.find(target.region);
    return area && area->definition.collision.collisionSegment(from, target.position, 0x0801);
}
std::optional<GroundLocation> System::placement(GroundLocation origin, const InventoryState &world) const {
    const auto *area = ports_.areas.find(origin.region);
    if (!area || !std::isfinite(origin.position.x) || !std::isfinite(origin.position.y)) return {};
    const auto global=origin.position+area->definition.origin;
    const int32_t x=int32_t(std::floor(global.x+.5f)),y=int32_t(std::floor(global.y+.5f));
    const auto room=ports_.areas.roomAtPosition(origin.region,origin.position+Vec{.5f,.5f},x,y);
    if(!room) return {};
    // Project item occupancy from the transaction draft, so split piles and
    // spills reserve each previous landing cell before the next search.
    std::set<std::tuple<int,int32_t,int32_t>> occupied;
    for(const auto &[id,item]:world.items) {
        (void)id;
        const auto *at=std::get_if<GroundLocation>(&item.location);
        const auto *owner=at?ports_.areas.find(at->region):nullptr;
        if(!owner || owner->definition.act!=area->definition.act) continue;
        const auto point=at->position+owner->definition.origin;
        occupied.emplace(owner->definition.act,int32_t(std::floor(point.x+.5f)),int32_t(std::floor(point.y+.5f)));
    }
    // Spatial supplies live unit/pet/portal masks; items come from the draft.
    const auto sample=ports_.spatial.nativeCollisionSample(0x3c01);
    const auto position=ports_.areas.nativeFreeSpace(*room,x,y,1,[&](RoomView owner,int32_t column,int32_t row) {
        auto mask=sample(owner,column,row);
        if(occupied.contains({ports_.areas.at(owner.area).definition.act,column,row})) mask|=0x200;
        return mask;
    });
    return position?std::optional<GroundLocation>{{position->room.area,position->position}}:std::nullopt;
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
    auto cursor=ports_.ids.cursor();
    const auto *area=ports_.areas.find(origin.region);
    if (!batch.items.empty() && (!area || !std::isfinite(origin.position.x) || !std::isfinite(origin.position.y)))
        return {DomainStatus::Conflict,{}};
    auto occupied=groundOccupancy(next.world,origin.region);
    for (auto &item : batch.items) {
        const auto position = groundPlacement(area->definition.collision,origin.position,occupied);
        if (!position) return {DomainStatus::Conflict, {}};
        occupied.emplace(int(position->x),int(position->y));
        auto properties = batch.equipment->items.at(item.id);
        item.id = EntityId{cursor++}; item.location = GroundLocation{origin.region, *position};item.revision=1;
        for(size_t index=0;index<item.socketedItems.size();++index) {
            auto &child=item.socketedItems[index];child.id=EntityId{cursor++};child.revision=1;child.location=SocketLocation{item.id,unsigned(index)};
        }
        next.equipment.items.emplace(item.id, std::move(properties));
        next.world.items.emplace(item.id, std::move(item));
    }
    next.equipment.includeSets(*batch.equipment);
    // Reserve identities only after every item has a valid landing cell.
    for(size_t index=0;index<identities;++index) ports_.ids.allocate();
    ++next.revision; return {DomainStatus::Applied, std::move(next)};
}
DomainResult<> System::install(PreparedBatch batch, GroundLocation origin) {
    auto next = prepare(std::move(batch), origin); if (!next) return {next.status, {}};
    commit(std::move(*next.value)); return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::installNativeGroundBatch(const std::vector<std::pair<PreparedBatch,GroundLocation>> &batch,uint64_t tick,std::span<const EntityId> identities,std::shared_ptr<EntityIds::Publication> allocation) {
    if(allocation && !ports_.ids.validate(*allocation)) return {DomainStatus::Stale,{}};
    if(batch.empty()) return {DomainStatus::Applied,std::monostate{}};
    if(allocation && identities.empty()) return {DomainStatus::InvalidRequest,{}};
    if(state_.revision==UINT64_MAX || state_.world.items.size()+batch.size()>4096 ||
        !identityCapacity(batch.size())) return {DomainStatus::Capacity,{}};
    if(!identities.empty()) {
        if(identities.size()!=batch.size()) return {DomainStatus::InvalidRequest,{}};
        uint64_t cursor=ports_.ids.cursor();
        for(const auto id:identities) {
            if((allocation && !allocation->covers(id)) || id.value<cursor || id.value>UINT32_MAX || state_.world.items.contains(id) || state_.equipment.items.contains(id))
                return {DomainStatus::InvalidRequest,{}};
            cursor=id.value+1;
        }
    }
    auto next=state_;auto ids=ports_.ids;
    std::vector<EventBatch> events;
    size_t index=0;
    for(const auto &[prepared,location]:batch) {
        if(!prepared.equipment || prepared.items.size()!=1 || !prepared.items.front().socketedItems.empty() ||
            !std::isfinite(location.position.x) || !std::isfinite(location.position.y))
            return {DomainStatus::InvalidRequest,{}};
        const auto *area=ports_.areas.find(location.region);
        if(!area || !ports_.areas.loaded(location.region,location.position) || !area->definition.activation.room(location.position))
            return {DomainStatus::Stale,{}};
        auto item=prepared.items.front();
        const auto values=prepared.equipment->items.find(item.id);
        if(values==prepared.equipment->items.end()) return {DomainStatus::InvalidRequest,{}};
        item.id=identities.empty()?ids.allocate():identities[index++];item.location=location;item.revision=1;
        next.equipment.items.emplace(item.id,values->second);
        next.equipment.includeSets(*prepared.equipment);
        next.world.items.emplace(item.id,item);
        if(events.empty() || events.back().audience.area!=location.region)
            events.push_back({0,tick,{}, {AudienceKind::Area,{},location.region},{}});
        events.back().facts.emplace_back(GroundDropFact{std::move(item)});
    }
    ++next.revision;
    const auto sent=ports_.events.publishGroup(std::move(events));
    if(!sent) return {sent.status,{}};
    if(allocation) ports_.ids.publish(*allocation);
    else ports_.ids=identities.empty()?ids:EntityIds(identities.back().value+1);
    commit(std::move(next));
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<PreparedBatch> System::prepareNativeGold(unsigned level,uint64_t &random,bool inStore) const {
    const auto *base=ports_.definitions?ports_.definitions->find("gld"):nullptr;
    if(!base || !base->equipment.isType("gold") || !level || level>99) return {DomainStatus::InvalidRequest,{}};
    auto nextRandom=random;
    auto unitRandom=initialRandom(rollRandom(nextRandom));
    ItemInstance item;item.id=EntityId{1};item.definition=base->code;
    item.nativeSeed=rollRandom(nextRandom);item.level=level;
    item.quantity=std::max(limitedRandom(unitRandom,5*level)+level,1u);
    item.nativeProperties=true;item.nativeFlags=inStore?0x82010:0x80010;
    if(!base->inventoryIcons.empty()) {
        item.nativeHasGraphic=true;
        item.nativeGraphic=limitedRandom(unitRandom,unsigned(base->inventoryIcons.size()));
    }
    auto equipment=std::make_shared<EquipmentRules>();
    EquipmentValues values;values.levels.resize(100);
    equipment->items.emplace(item.id,std::move(values));
    random=nextRandom;
    return {DomainStatus::Applied,PreparedBatch{{std::move(item)},std::move(equipment),{}}};
}
DomainResult<EntityId> System::createNativeGold(GroundLocation origin,unsigned level,uint64_t tick) {
    const auto *area=ports_.areas.find(origin.region);
    const auto *base=ports_.definitions?ports_.definitions->find("gld"):nullptr;
    if(!area || !base || !base->equipment.isType("gold") || !level || level>99 ||
        !std::isfinite(origin.position.x) || !std::isfinite(origin.position.y)) return {DomainStatus::InvalidRequest,{}};
    origin.position={std::floor(origin.position.x),std::floor(origin.position.y)};
    if(!area->definition.activation.room(origin.position)) return {DomainStatus::InvalidRequest,{}};
    if(state_.revision==UINT64_MAX || state_.world.items.size()>=4096 || !identityCapacity(1) ||
        !ports_.events.hasCapacity(1,1)) return {DomainStatus::Capacity,{}};
    auto random=ports_.random;
    auto prepared=prepareNativeGold(level,random,true);
    if(!prepared) return {prepared.status,{}};
    auto item=std::move(prepared.value->items.front());item.id=EntityId{ports_.ids.cursor()};item.location=origin;
    auto next=state_;
    next.world.items.emplace(item.id,item);
    EquipmentValues values;values.levels.resize(100);
    next.equipment.items.emplace(item.id,std::move(values));
    ++next.revision;
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},origin.region},{GroundDropFact{item}}});
    if(!sent) return {sent.status,{}};
    ports_.ids.allocate();ports_.random=random;commit(std::move(next));
    return {DomainStatus::Applied,item.id};
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
DomainResult<> System::restoreGround(EntityId id,uint64_t tick) {
    const auto found=state_.world.items.find(id);
    if(found==state_.world.items.end()) {clearRestoration(id);return {DomainStatus::Applied,std::monostate{}};}
    auto &item=found->second;const auto *ground=std::get_if<GroundLocation>(&item.location);
    const auto *base=ports_.definitions?ports_.definitions->find(item.definition):nullptr;
    if(!ground || !base || !state_.equipment.items.contains(id)) {clearRestoration(id);return {DomainStatus::Applied,std::monostate{}};}
    const auto &values=state_.equipment.at(id,1);
    const auto rule=itemRestoration(item,*base,values.stats,values.maximumDurability);
    if(!rule || (rule->quantity?item.quantity:item.durability)>=rule->maximum) {clearRestoration(id);return {DomainStatus::Applied,std::monostate{}};}
    if(item.revision==UINT64_MAX || state_.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},ground->region},{GroundRestoredFact{id,item.revision+1}}});
    if(!sent) return {sent.status,{}};
    ++(rule->quantity?item.quantity:item.durability);++item.revision;++state_.revision;
    rearmRestoration(id,*rule,tick);return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::step(TickContext tick,FrameFacts &) {
    bool blocked=false;
    std::set<EntityId> present;
    for(const auto &[id,item]:state_.world.items) present.insert(id);
    for(const auto &[id,player]:ports_.players.all()) for(const auto &[itemId,item]:player.persistent.inventory.items) present.insert(itemId);
    std::vector<EntityId> stale;for(const auto &[id,clock]:restoration_) if(!present.contains(id)) stale.push_back(id);
    for(const auto id:stale) clearRestoration(id);
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
                    restorationClock(item.id,*rule,tick.tick);

                } else clearRestoration(item.id);
            } else clearRestoration(item.id);
        }
        if(!base || !ground || base->questTag) {++it;continue;}
        if(tick.tick<lifetime->second.expires) {++it;continue;}
        if(state_.revision==UINT64_MAX || !ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},ground->region},{GroundRemoveFact{item.id}}})) {blocked=true;++it;continue;}
        groundLifetimes_.erase(item.id);clearRestoration(item.id);state_.goldOwners.erase(item.id);state_.equipment.items.erase(item.id);it=state_.world.items.erase(it);++state_.revision;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
