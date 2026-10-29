#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/spatial/system.hpp"
#include "core/random.hpp"
#include "gameplay/items/quality.hpp"
#include "gameplay/items/inventory_order.hpp"
#include <cmath>
#include <algorithm>
#include <tuple>
namespace d2x::server::items {
namespace {
uint64_t groundExpiration(const ItemInstance &item,const ItemDefinition &base,uint64_t tick) {
    if(base.questTag) return 0;
    const bool rare=item.quality==ItemQuality::Rare || item.quality==ItemQuality::Set || item.quality==ItemQuality::Unique || item.quality==ItemQuality::Crafted || item.quality==ItemQuality::Tempered;
    const uint64_t duration=rare || (base.equipment.isType("gold") && item.quantity>10000)?45000:base.equipment.isType("sock")?30000:15000;
    return tick>UINT64_MAX-duration?UINT64_MAX:tick+duration;
}
D2sItem savedGroundItem(const ItemInstance &item,const NativeItemSaveRules &rules,std::vector<InactiveItem::Derived> &derived) {
    if(!item.nativeProperties) throw std::invalid_argument("Ground item lacks native saved attributes");
    if(item.level>127 || (item.ear && item.ear->level>127)) throw std::invalid_argument("Ground item requires attribute reconstruction after level saturation");
    const auto &base=rules.bases.at(item.definition);
    derived.push_back({item.requiredLevel,item.socketRequiredLevel});
    D2sItem saved;saved.code=item.definition;saved.seed=item.nativeSeed;saved.format=item.nativeFormat;
    saved.flags=(item.nativeFlags&~0x80110u)|0x800000u|(item.identified?0x10u:0u);
    if(base.definition.maxDurability && !item.durability) saved.flags|=0x100u;
    if(base.compact) saved.flags|=0x200000u;
    saved.level=std::min(item.level,127u);saved.quality=unsigned(std::find(nativeItemQualities.begin()+1,nativeItemQualities.end(),item.quality)-nativeItemQualities.begin());
    saved.hasGraphic=item.nativeHasGraphic;saved.graphic=item.nativeGraphic;saved.autoAffix=item.nativeAutoAffix;
    saved.defense=unsigned(item.defense);saved.maxDurability=item.nativeMaxDurability;saved.durability=item.durability;
    saved.quantity=base.definition.bookCapacity?item.charges:item.quantity;saved.questDifficulty=item.nativeQuestDifficulty;
    saved.sockets=item.sockets;if(item.sockets) saved.flags|=0x800u;
    saved.personalizedName=item.personalizedName;if(!saved.personalizedName.empty()) saved.flags|=0x1000000u;
    saved.ear=item.ear;if(saved.ear) {saved.flags|=0x10000u;saved.ear->level=std::min(saved.ear->level,127u);}
    saved.realmIdentity=item.realmIdentity;
    saved.fileIndex=unsigned(std::max(0,item.specialRow>=0?item.specialRow:item.gradeRow));
    if(item.quality==ItemQuality::Normal && base.definition.equipment.isType("body") && !base.definition.equipment.isType("play")) saved.fileIndex=item.nativeBodyPart;
    size_t prefix{},suffix{};
    for(const auto &affix:item.affixes) {
        auto &index=affix.prefix?prefix:suffix;
        if(index>=3 || affix.row<0) throw std::invalid_argument("Invalid ground item affix");
        (affix.prefix?saved.prefixes:saved.suffixes)[index++]=unsigned(affix.row+1);
    }
    if(item.quality==ItemQuality::Rare || item.quality==ItemQuality::Crafted || item.quality==ItemQuality::Tempered) {
        saved.rarePrefix=item.rarePrefixRow<0?0:unsigned(item.rarePrefixRow+1+rules.rareSuffixCount);
        saved.rareSuffix=item.rareSuffixRow<0?0:unsigned(item.rareSuffixRow+1);
    }
    saved.book=item.definition=="ibk" || item.definition=="isc"?1:0;
    const auto copy=[](const auto &source,auto &destination) {for(const auto &stat:source) destination.push_back({uint16_t(stat.id),stat.value,stat.parameter});};
    copy(item.savedStats,saved.stats);copy(item.runewordStats,saved.runewordStats);
    for(size_t index=0;index<item.savedSetStats.size();++index) copy(item.savedSetStats[index],saved.setStats[index]);
    if(item.runewordRow>=0) {saved.runewordId=rules.runewordIds.at(item.runewordRow);saved.flags|=0x4000000u;}
    for(size_t index=0;index<item.socketedItems.size();++index) {
        auto child=savedGroundItem(item.socketedItems[index],rules,derived);child.mode=6;child.page=0;child.x=unsigned(index);
        saved.socketedItems.push_back(std::move(child));
    }
    return saved;
}
ItemInstance restoredGroundItem(const D2sItem &saved,const NativeItemSaveRules &rules,const std::vector<InactiveItem::Derived> &derived,size_t &index) {
    const auto &base=rules.bases.at(saved.code).definition;
    ItemInstance item;item.definition=saved.code;item.nativeProperties=true;item.nativeSeed=saved.seed;item.nativeFormat=saved.format;
    item.nativeFlags=saved.flags;item.identified=(saved.flags&0x10u)!=0;item.level=saved.level;item.quality=nativeItemQualities.at(saved.quality);
    item.requiredLevel=derived.at(index).requiredLevel;item.socketRequiredLevel=derived.at(index++).socketRequiredLevel;
    item.nativeGraphic=saved.graphic;item.nativeHasGraphic=saved.hasGraphic;item.nativeAutoAffix=saved.autoAffix;
    item.defense=int(saved.defense);item.nativeMaxDurability=saved.maxDurability;item.durability=saved.durability;
    item.quantity=base.bookCapacity?1:saved.quantity;item.charges=base.bookCapacity?saved.quantity:0;
    item.nativeQuestDifficulty=saved.questDifficulty;item.sockets=saved.sockets;item.personalizedName=saved.personalizedName;
    item.ear=saved.ear;item.realmIdentity=saved.realmIdentity;
    if(item.quality==ItemQuality::Unique || item.quality==ItemQuality::Set) item.specialRow=int(saved.fileIndex);
    else if(item.quality==ItemQuality::Inferior || item.quality==ItemQuality::Superior) item.gradeRow=int(saved.fileIndex);
    else if(item.quality==ItemQuality::Normal && base.equipment.isType("body") && !base.equipment.isType("play")) item.nativeBodyPart=saved.fileIndex;
    for(const bool prefix:{true,false}) for(const auto affix:prefix?saved.prefixes:saved.suffixes)
        if(affix) item.affixes.push_back({prefix,int32_t(affix-1),{}});
    item.rarePrefixRow=saved.rarePrefix?int(saved.rarePrefix)-int(rules.rareSuffixCount)-1:-1;
    item.rareSuffixRow=saved.rareSuffix?int(saved.rareSuffix)-1:-1;
    if(saved.runewordId) {
        const auto word=std::find_if(rules.runewordIds.begin(),rules.runewordIds.end(),[&](const auto &entry){return entry.second==saved.runewordId;});
        if(word==rules.runewordIds.end()) throw std::invalid_argument("Unknown inactive runeword");
        item.runewordRow=word->first;
    }
    const auto copy=[](const auto &source,auto &destination) {
        for(const auto &stat:source) {
            if(stat.value<INT32_MIN || stat.value>INT32_MAX) throw std::invalid_argument("Inactive item stat out of range");
            destination.push_back({int(stat.id),stat.parameter,int(stat.value)});
        }
    };
    copy(saved.stats,item.savedStats);copy(saved.runewordStats,item.runewordStats);
    for(size_t set=0;set<saved.setStats.size();++set) copy(saved.setStats[set],item.savedSetStats[set]);
    for(const auto &child:saved.socketedItems) item.socketedItems.push_back(restoredGroundItem(child,rules,derived,index));
    return item;
}
}
std::optional<InactiveItemRequest> System::pendingInactiveItem() {
    std::erase_if(inactivePreparation_,[&](const auto &entry) {
        const auto item=state_.world.items.find(entry.first);
        return item==state_.world.items.end() || item->second.revision!=entry.second.source.item.revision ||
            item->second.location!=entry.second.source.item.location;
    });
    for(const auto &[id,entry]:inactivePreparation_) {(void)id;if(!entry.prepared) return entry.source;}
    return {};
}
DomainResult<> System::prepareInactiveItem(PreparedInactiveItem prepared) {
    const auto found=inactivePreparation_.find(prepared.source.item.id);
    if(found==inactivePreparation_.end() || found->second.source.token!=prepared.source.token) return {DomainStatus::Stale,{}};
    const auto item=state_.world.items.find(prepared.source.item.id);
    if(item==state_.world.items.end() || item->second.revision!=found->second.source.item.revision ||
        item->second.location!=found->second.source.item.location) {inactivePreparation_.erase(found);return {DomainStatus::Stale,{}};}
    if(prepared.deferred.empty()) {
        if(!ports_.itemSave || prepared.node.bitstream.empty() || prepared.node.equipment.levels.empty()) return {DomainStatus::InvalidRequest,{}};
        try {
            const auto decoded=readNativeSavedItem(prepared.node.bitstream,*ports_.itemSave,true);
            if(decoded.bytesRead!=prepared.node.bitstream.size() || decoded.item.mode!=3 || decoded.item.code!=item->second.definition ||
                decoded.item.x!=unsigned(std::clamp(int(found->second.source.global.x),0,65535)) ||
                decoded.item.y!=unsigned(std::clamp(int(found->second.source.global.y),0,65535))) return {DomainStatus::InvalidRequest,{}};
            size_t index{};restoredGroundItem(decoded.item,*ports_.itemSave,prepared.node.derived,index);
            if(index!=prepared.node.derived.size()) return {DomainStatus::InvalidRequest,{}};
        } catch(const std::exception &) {return {DomainStatus::InvalidRequest,{}};}
    }
    prepared.source=found->second.source;found->second.prepared=std::move(prepared);
    return {DomainStatus::Applied,std::monostate{}};
}
void System::prepareGroundOrder(State &next,std::span<const DomainFact> facts) const {
    next.world.itemOrder=inventoryItemOrder(next.world);
    for(const auto &fact:facts) if(const auto *drop=std::get_if<GroundDropFact>(&fact)) {
        const auto found=next.world.items.find(drop->item.id);
        if(found==next.world.items.end() || !std::holds_alternative<GroundLocation>(found->second.location)) continue;
        const auto previous=state_.world.items.find(drop->item.id);
        if(previous!=state_.world.items.end() && previous->second.location==found->second.location) continue;
        reinsertInventoryItem(next.world,drop->item.id);
    }
    prepareGroundRooms(next);
}
void System::prepareGroundRooms(State &next) const {
    next.groundRegistrations.clear();
    for(const auto id:inventoryItemOrder(next.world)) {
        const auto &item=next.world.items.at(id);
        const auto *ground=std::get_if<GroundLocation>(&item.location);
        if(!ground) continue;
        const auto previous=state_.world.items.find(id);
        if(previous!=state_.world.items.end() && previous->second.location==item.location) continue;
        next.groundRegistrations.emplace_back(id,ports_.areas.hasRoomUnit(id)?nullptr:
            ports_.areas.prepareRoomUnit(id,4,ground->region,ground->position));
    }
}
DomainResult<RoomCompression> System::prepareRoomCompression(RegionId region,size_t index,uint64_t tick,EntityId only) {
    inactiveFailure_.clear();
    const auto *area=ports_.areas.find(region);
    if(!area || index>=area->rooms.size() || !area->rooms[index].loaded) return {DomainStatus::Stale,{}};
    if(state_.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto &bounds=area->definition.activation.rooms().at(index);
    RoomCompression plan;plan.expectedRevision=state_.revision;
    auto &saved=plan.rooms[{region,index}];
    if(const auto previous=inactive_.find({region,index});previous!=inactive_.end()) saved=previous->second;
    const RoomView room{region,(int(area->definition.origin.x)+bounds.x)/5,(int(area->definition.origin.y)+bounds.y)/5};
    for(const auto id:ports_.areas.roomUnitOrder(room)) {
        if(only && id!=only) continue;
        const auto found=state_.world.items.find(id);if(found==state_.world.items.end()) continue;
        const auto &item=found->second;
        const auto *ground=std::get_if<GroundLocation>(&item.location);
        if(!ground || ground->region!=region || area->definition.activation.room(ground->position)!=&bounds) continue;
        const auto *base=ports_.definitions?ports_.definitions->find(item.definition):nullptr;
        const auto values=state_.equipment.items.find(id);
        if(!base || values==state_.equipment.items.end()) return {DomainStatus::Unavailable,{}};
        const auto expires=groundExpiration(item,*base,tick);
        if(!expires || tick<=expires) {
            if(!ports_.itemSave) {inactiveFailure_="Missing native item save rules";return {DomainStatus::NotImplemented,{}};}
            InactiveItem node;node.equipment=values->second;node.expires=expires;
            try {
                auto encoded=savedGroundItem(item,*ports_.itemSave,node.derived);
                const auto global=ground->position+area->definition.origin;
                encoded.mode=3;encoded.page=0;encoded.x=unsigned(std::clamp(int(global.x),0,65535));encoded.y=unsigned(std::clamp(int(global.y),0,65535));
                node.bitstream=writeNativeSavedItem(encoded,*ports_.itemSave,true);
                const auto decoded=readNativeSavedItem(node.bitstream,*ports_.itemSave,true);
                if(decoded.bytesRead!=node.bitstream.size()) return {DomainStatus::InvalidRequest,{}};
                const auto sameStats=[](const auto &before,const auto &after) {
                    std::map<std::pair<uint16_t,int32_t>,int64_t> first,second;
                    for(const auto &stat:before) if(stat.value) first[{stat.id,stat.parameter}]=stat.value;
                    for(const auto &stat:after) if(stat.value) second[{stat.id,stat.parameter}]=stat.value;
                    return first==second;
                };
                const auto preserved=[&](auto &&self,const D2sItem &before,const D2sItem &after)->bool {
                    if((!(before.flags&0x200000u) && before.level!=after.level) || before.defense!=after.defense || before.maxDurability!=after.maxDurability ||
                        before.durability!=after.durability || before.quantity!=after.quantity || before.sockets!=after.sockets ||
                        before.questDifficulty!=after.questDifficulty || !sameStats(before.stats,after.stats) ||
                        !sameStats(before.runewordStats,after.runewordStats) || before.socketedItems.size()!=after.socketedItems.size()) return false;
                    for(size_t set=0;set<before.setStats.size();++set) if(!sameStats(before.setStats[set],after.setStats[set])) return false;
                    for(size_t socket=0;socket<before.socketedItems.size();++socket)
                        if(!self(self,before.socketedItems[socket],after.socketedItems[socket])) return false;
                    return true;
                };
                if(!preserved(preserved,encoded,decoded.item)) throw std::invalid_argument("Ground item requires attribute reconstruction after saved-field saturation");
            } catch(const std::exception &error) {
                inactiveFailure_="Item "+std::to_string(id.value)+" ("+item.definition+"): "+error.what();
                auto pending=inactivePreparation_.find(id);
                if(pending==inactivePreparation_.end() || pending->second.source.item.revision!=item.revision || pending->second.source.item.location!=item.location) {
                    if(nextInactiveToken_==UINT64_MAX) return {DomainStatus::Capacity,{}};
                    inactivePreparation_.insert_or_assign(id,InactivePreparation{{nextInactiveToken_++,item,ground->position+area->definition.origin},{}});
                    return {DomainStatus::PendingContent,{}};
                }
                if(!pending->second.prepared) return {DomainStatus::PendingContent,{}};
                if(!pending->second.prepared->deferred.empty()) {inactiveFailure_=pending->second.prepared->deferred;return {DomainStatus::NotImplemented,{}};}
                node=pending->second.prepared->node;node.expires=expires;
            }
            if(const auto owner=state_.goldOwners.find(id);owner!=state_.goldOwners.end()) node.goldOwner=owner->second;
            saved.insert(saved.begin(),std::move(node));
        }
        plan.retired.push_back(id);
    }
    if(!plan.retired.empty()) {
        plan.next=state_;
        for(const auto id:plan.retired) {
            plan.next.world.items.erase(id);plan.next.equipment.items.erase(id);plan.next.goldOwners.erase(id);
        }
        std::erase_if(plan.next.world.itemOrder,[&](EntityId id){return !plan.next.world.items.contains(id);});
        ++plan.next.revision;
    }
    return {DomainStatus::Applied,std::move(plan)};
}
void System::retireCompressedItem(const RoomCompression &plan,EntityId id) noexcept {
    if(std::find(plan.retired.begin(),plan.retired.end(),id)==plan.retired.end()) return;
    if(plan.expectedRevision!=state_.revision) std::terminate();
    clearRestoration(id);inactivePreparation_.erase(id);groundLifetimes_.erase(id);
    ports_.events.discardUnitSound(id);ports_.areas.removeRoomUnit(id);
    state_.world.items.erase(id);state_.equipment.items.erase(id);state_.goldOwners.erase(id);
    std::erase(state_.world.itemOrder,id);
}
void System::publishRoomCompression(RoomCompression plan) noexcept {
    if(plan.expectedRevision!=state_.revision || plan.rooms.size()!=1) std::terminate();
    for(const auto id:plan.retired) if(state_.world.items.contains(id) || ports_.areas.hasRoomUnit(id)) std::terminate();
    auto node=plan.rooms.extract(plan.rooms.begin());
    if(node.mapped().empty()) inactive_.erase(node.key());
    else if(const auto previous=inactive_.find(node.key());previous!=inactive_.end()) previous->second.swap(node.mapped());
    else inactive_.insert(std::move(node));
    if(!plan.retired.empty()) commit(std::move(plan.next));
}
StepStatus System::restoreRoom(RegionId region,size_t index,uint64_t tick) {
    const auto found=inactive_.find({region,index});if(found==inactive_.end()) return StepStatus::Complete;
    const auto *area=ports_.areas.find(region);
    if(!area || index>=area->rooms.size() || !area->rooms[index].loaded) return StepStatus::Blocked;
    auto &saved=found->second;
    while(!saved.empty()) {
        const auto &source=saved.front();
        if(source.expires && tick>source.expires) {saved.erase(saved.begin());continue;}
        if(state_.revision==UINT64_MAX || state_.world.items.size()>=4096) return StepStatus::Blocked;
        size_t count{};
        const auto countItems=[&](auto &&self,const ItemInstance &item)->void {
            ++count;for(const auto &child:item.socketedItems) self(self,child);
        };
        if(!ports_.itemSave) return StepStatus::NotImplemented;
        const auto decoded=readNativeSavedItem(source.bitstream,*ports_.itemSave,true);
        size_t derivedIndex{};
        auto item=restoredGroundItem(decoded.item,*ports_.itemSave,source.derived,derivedIndex);
        item.location=GroundLocation{region,Vec{float(decoded.item.x),float(decoded.item.y)}-area->definition.origin};
        countItems(countItems,item);
        if(!identityCapacity(count)) return StepStatus::Blocked;
        auto allocation=ports_.ids.preparePublication(count);
        auto ids=ports_.ids;auto random=ports_.random;
        const auto identify=[&](auto &&self,ItemInstance &value)->void {
            childRandom(random);rollRandom(random);
            value.id=ids.allocate();value.revision=1;value.nativePropertyRandom.reset();
            allocation->bind(value.id,NativeUnitType::Item);
            value.nativeFlags=(value.nativeFlags|0x80000u)&~0x2000u;
            for(size_t socket=0;socket<value.socketedItems.size();++socket) {
                auto &child=value.socketedItems[socket];self(self,child);child.location=SocketLocation{value.id,unsigned(socket)};
            }
        };
        identify(identify,item);
        auto next=state_;next.world.items.emplace(item.id,item);next.equipment.items.emplace(item.id,source.equipment);
        next.world.itemOrder.push_back(item.id);
        if(source.goldOwner) next.goldOwners.emplace(item.id,*source.goldOwner);
        ++next.revision;
        auto lifetimes=groundLifetimes_;
        const auto minimum=tick>UINT64_MAX-15000?UINT64_MAX:tick+15000;
        lifetimes.emplace(item.id,GroundLifetime{item.revision,source.expires?std::max(source.expires,minimum):0});
        prepareGroundRooms(next);
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},region},{GroundDropFact{item,false}}});
        if(!sent) return StepStatus::Blocked;
        ports_.ids.publish(*allocation);
        commit(std::move(next));groundLifetimes_.swap(lifetimes);ports_.random=random;
        saved.erase(saved.begin());
    }
    inactive_.erase(found);return StepStatus::Complete;
}
void System::commit(State next) noexcept {
        std::erase_if(next.goldOwners,[&](const auto &entry){return !next.world.items.contains(entry.first);});
        std::erase_if(groundLifetimes_,[&](const auto &entry){
            const auto item=next.world.items.find(entry.first);
            return item==next.world.items.end() || item->second.location!=state_.world.items.at(entry.first).location;
        });
        for(const auto &[id,item]:state_.world.items) if(std::holds_alternative<GroundLocation>(item.location)) {
            const auto found=next.world.items.find(id);
            if(found==next.world.items.end() || !std::holds_alternative<GroundLocation>(found->second.location)) {
                ports_.events.discardUnitSound(id);ports_.areas.removeRoomUnit(id);
            }
        }
        for(const auto &[id,registration]:next.groundRegistrations) {
            if(registration) {
                if(!ports_.areas.validateRoomUnit(*registration,id)) std::terminate();
                ports_.areas.publishRoomUnit(*registration);
            }
            const auto &at=std::get<GroundLocation>(next.world.items.at(id).location);
            ports_.areas.setUnitCollision(id,{});
            ports_.areas.moveRoomUnit(id,at.region,at.position);
            ports_.areas.setUnitCollision(id,ports_.areas.rectangleFootprint(at.region,at.position,1,1,0x200));
        }
        next.groundRegistrations.clear();
        std::swap(state_, next);
    }

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
    const int32_t x=int32_t(std::floor(global.x)),y=int32_t(std::floor(global.y));
    const auto observer=Vec{float(x)+.5f,float(y)+.5f}-area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(origin.region,observer,x,y);
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
        occupied.emplace(owner->definition.act,int32_t(std::floor(point.x)),int32_t(std::floor(point.y)));
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
    std::set<EntityId> reserved;
    for(const auto &item:batch.items) {
        const auto found=batch.identities.find(item.id);
        if(found!=batch.identities.end()) {
            const auto id=found->second;
            if(!id || id.value>=ports_.ids.cursor() || !ports_.ids.hasNative(id,NativeUnitType::Item) ||
               state_.world.items.contains(id) || state_.equipment.items.contains(id) || !reserved.insert(id).second)
                return {DomainStatus::InvalidRequest,{}};
        } else ++identities;
        identities+=item.socketedItems.size();
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
        const auto reservedId=batch.identities.find(item.id);
        item.id = reservedId==batch.identities.end()?EntityId{cursor++}:reservedId->second;
        item.location = GroundLocation{origin.region, *position};item.revision=1;
        for(size_t index=0;index<item.socketedItems.size();++index) {
            auto &child=item.socketedItems[index];child.id=EntityId{cursor++};child.revision=1;child.location=SocketLocation{item.id,unsigned(index)};
        }
        next.equipment.items.emplace(item.id, std::move(properties));
        next.world.itemOrder.push_back(item.id);
        next.world.items.emplace(item.id, std::move(item));
    }
    next.equipment.includeSets(*batch.equipment);
    prepareGroundRooms(next);
    // New non-loot identities publish here. Loot retains its earlier native
    // allocation even when generation or landing subsequently fails.
    publishIdentities(ports_.ids.cursor(),identities);
    ++next.revision; return {DomainStatus::Applied, std::move(next)};
}
DomainResult<> System::install(PreparedBatch batch, GroundLocation origin) {
    auto next = prepare(std::move(batch), origin); if (!next) return {next.status, {}};
    commit(std::move(*next.value)); return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::installNativeGroundBatch(const std::vector<std::pair<PreparedBatch,GroundLocation>> &batch,uint64_t tick,std::span<const EntityId> identities,std::shared_ptr<EntityIds::Publication> allocation,bool flip,std::optional<PlayerId> goldOwner) {
    if(allocation && !ports_.ids.validate(*allocation)) return {DomainStatus::Stale,{}};
    if(batch.empty()) return {DomainStatus::Applied,std::monostate{}};
    if(allocation && identities.empty()) return {DomainStatus::InvalidRequest,{}};
    size_t fresh{};std::set<EntityId> reserved;
    for(const auto &[prepared,location]:batch) {
        (void)location;
        if(prepared.identities.empty()) {++fresh;continue;}
        if(!identities.empty() || prepared.items.size()!=1 || prepared.identities.size()!=1) return {DomainStatus::InvalidRequest,{}};
        const auto found=prepared.identities.find(prepared.items.front().id);
        if(found==prepared.identities.end()) return {DomainStatus::InvalidRequest,{}};
        const auto id=found->second;
        if(!id || id.value>=ports_.ids.cursor() || !ports_.ids.hasNative(id,NativeUnitType::Item) ||
           state_.world.items.contains(id) || state_.equipment.items.contains(id) || !reserved.insert(id).second)
            return {DomainStatus::InvalidRequest,{}};
    }
    if(state_.revision==UINT64_MAX || state_.world.items.size()+batch.size()>4096 ||
        !identityCapacity(fresh)) return {DomainStatus::Capacity,{}};
    if(!identities.empty()) {
        if(identities.size()!=batch.size()) return {DomainStatus::InvalidRequest,{}};
        uint64_t cursor=ports_.ids.cursor();
        for(const auto id:identities) {
            if((allocation && !allocation->covers(id)) || id.value<cursor || id.value>UINT32_MAX || state_.world.items.contains(id) || state_.equipment.items.contains(id))
                return {DomainStatus::InvalidRequest,{}};
            cursor=id.value+1;
        }
    }
    if(!allocation) allocation=ports_.ids.preparePublication(identities.empty()?fresh:size_t(identities.back().value+1-ports_.ids.cursor()));
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
        if(goldOwner) {
            const auto *base=ports_.definitions?ports_.definitions->find(item.definition):nullptr;
            if(!base || !base->equipment.isType("gold")) return {DomainStatus::InvalidRequest,{}};
        }
        const auto values=prepared.equipment->items.find(item.id);
        if(values==prepared.equipment->items.end()) return {DomainStatus::InvalidRequest,{}};
        const auto reservedId=prepared.identities.find(item.id);
        item.id=reservedId!=prepared.identities.end()?reservedId->second:identities.empty()?ids.allocate():identities[index++];
        item.location=location;item.revision=1;
        next.equipment.items.emplace(item.id,values->second);
        next.equipment.includeSets(*prepared.equipment);
        next.world.items.emplace(item.id,item);
        if(goldOwner) next.goldOwners.emplace(item.id,*goldOwner);
        next.world.itemOrder.push_back(item.id);
        if(allocation) allocation->bind(item.id,NativeUnitType::Item);
        if(events.empty() || events.back().audience.area!=location.region)
            events.push_back({0,tick,{}, {AudienceKind::Area,{},location.region},{}});
        events.back().facts.emplace_back(GroundDropFact{std::move(item),flip});
    }
    ++next.revision;
    prepareGroundRooms(next);
    const auto sent=ports_.events.publishGroup(std::move(events));
    if(!sent) return {sent.status,{}};
    if(allocation) ports_.ids.publish(*allocation);
    else if(identities.empty()) ports_.ids=ids;
    else ports_.ids.advanceTo(identities.back().value+1);
    commit(std::move(next));
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<PreparedBatch> System::prepareNativeGold(unsigned level,uint64_t &random,bool inStore) const {
    const auto *base=ports_.definitions?ports_.definitions->find("gld"):nullptr;
    if(!base || !base->equipment.isType("gold") || !level || level>99) return {DomainStatus::InvalidRequest,{}};
    auto nextRandom=random;
    const auto unitSeed=rollRandom(nextRandom);auto unitRandom=initialRandom(unitSeed);
    ItemInstance item;item.id=EntityId{1};item.definition=base->code;
    item.nativeUnitSeed=unitSeed;item.nativeSeed=rollRandom(nextRandom);item.level=level;
    item.nativePropertyRandom=initialRandom(item.nativeSeed);
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
DomainResult<System::PlayerGoldSelection> System::selectPlayerGold(RegionId region,Vec position) {
    const auto *area=ports_.areas.find(region);if(!area || !ports_.definitions) return {DomainStatus::Unavailable,{}};
    const auto global=position+area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(region,position,int(global.x),int(global.y));
    if(!room) return {DomainStatus::Unavailable,{}};
    const unsigned level=unsigned(area->definition.town?1:std::max(1,area->definition.monsterLevel-1));
    std::vector<const ItemDefinition *> choices;
    for(const auto &[code,base]:ports_.definitions->entries()) {
        (void)code;
        if(base.family!=ItemFamily::Misc || base.base.type!="gold" || base.questTag) continue;
        if(!base.base.spawnable || !base.base.level) return {DomainStatus::NotImplemented,{}};
        if(*base.base.spawnable && *base.base.level<=int(level)) choices.push_back(&base);
    }
    if(choices.empty()) return {DomainStatus::Unavailable,{}};
    std::sort(choices.begin(),choices.end(),[](const auto *left,const auto *right){return left->base.sourceRow<right->base.sourceRow;});
    if(choices.size()>1023) choices.resize(1023);
    const auto borrowed=ports_.areas.roomRandom(*room);if(!borrowed) return {borrowed.status,{}};
    auto loan=*borrowed.value;
    // ItemMode::sub_6FC4C3D0 rolls even for a single eligible Misc row.
    const auto *selected=choices.at(limitedRandom(loan.next,unsigned(choices.size())));
    ports_.areas.publishRoomRandom(loan);
    return {DomainStatus::Applied,PlayerGoldSelection{selected->code,level}};
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
    next.world.itemOrder.push_back(item.id);
    EquipmentValues values;values.levels.resize(100);
    next.equipment.items.emplace(item.id,std::move(values));
    ++next.revision;
    prepareGroundRooms(next);
    auto allocation=ports_.ids.preparePublication(1);allocation->bind(item.id,NativeUnitType::Item);
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},origin.region},{GroundDropFact{item}}});
    if(!sent) return {sent.status,{}};
    ports_.ids.publish(*allocation);ports_.random=random;commit(std::move(next));
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
    if(!rule || !itemRestorationActive(item,*rule)) {clearRestoration(id);return {DomainStatus::Applied,std::monostate{}};}
    if(rule->charges && itemRestorationValue(item,*rule)>=rule->maximum) {rearmRestoration(id,*rule,tick);return {DomainStatus::Applied,std::monostate{}};}
    if(item.revision==UINT64_MAX || state_.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto candidate=item;
    std::optional<EquipmentValues> equipment;
    if(rule->charges) {
        const auto restored=pd2RestoreCharge(candidate,rule->layer);
        if(!restored) return {DomainStatus::Stale,{}};
        equipment=state_.equipment.items.at(id);
        for(auto &level:equipment->levels) pd2WriteChargeValues(level.stats,candidate,*restored);
    } else ++(rule->quantity?candidate.quantity:candidate.durability);
    ++candidate.revision;
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},ground->region},{GroundRestoredFact{id,item.revision+1}}});
    if(!sent) return {sent.status,{}};
    item=std::move(candidate);
    if(equipment) state_.equipment.items.at(id)=std::move(*equipment);
    ++state_.revision;
    rearmRestoration(id,*rule,tick);return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::step(TickContext tick,FrameFacts &) {
    bool blocked=false;
    std::erase_if(inactivePreparation_,[&](const auto &entry) {
        const auto item=state_.world.items.find(entry.first);
        return item==state_.world.items.end() || item->second.revision!=entry.second.source.item.revision ||
            item->second.location!=entry.second.source.item.location;
    });
    // GAME_Update sweeps both active items and compressed nodes every 1500
    // frames. Reentry still checks a node's deadline immediately in restoreRoom.
    const bool sweep=tick.tick%1500==0;
    if(sweep) {
        for(auto &[room,items]:inactive_) {
            (void)room;
            std::erase_if(items,[&](const auto &item){return item.expires && tick.tick>item.expires;});
        }
        std::erase_if(inactive_,[](const auto &room){return room.second.empty();});
    }
    std::set<EntityId> present;
    for(const auto &[id,item]:state_.world.items) present.insert(id);
    for(const auto &[id,player]:ports_.players.all()) {
        for(const auto &[itemId,item]:player.persistent.inventory.items) present.insert(itemId);
        if(player.persistent.ironGolem) present.insert(player.persistent.ironGolem->id);
    }
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
            if(inserted) entry->second.expires=groundExpiration(item,*base,tick.tick);
        }
        if(base && ground && state_.equipment.items.contains(item.id)) {
            const auto &values=state_.equipment.at(item.id,1);
            if(const auto rule=itemRestoration(item,*base,values.stats,values.maximumDurability)) {
                if(itemRestorationActive(item,*rule)) {
                    restorationClock(item.id,*rule,tick.tick);

                } else clearRestoration(item.id);
            } else clearRestoration(item.id);
        }
        if(!base || !ground || base->questTag) {++it;continue;}
        if(!sweep || tick.tick<lifetime->second.expires) {++it;continue;}
        if(state_.revision==UINT64_MAX || !ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},ground->region},{GroundRemoveFact{item.id}}})) {blocked=true;++it;continue;}
        ports_.events.discardUnitSound(item.id);ports_.areas.removeRoomUnit(item.id);
        std::erase(state_.world.itemOrder,item.id);
        groundLifetimes_.erase(item.id);clearRestoration(item.id);state_.goldOwners.erase(item.id);state_.equipment.items.erase(item.id);it=state_.world.items.erase(it);++state_.revision;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
