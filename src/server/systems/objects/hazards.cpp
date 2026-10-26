#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/units/distance.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace d2x::server::objects {
void System::initializeControl() {
    if(state_.controlRandom) throw std::logic_error("Object control is already initialized");
    state_.controlRandom=childRandom(ports_.random);
}
void System::initialize(Object &object,uint64_t tick,uint64_t &random,uint64_t &controlRandom) {
    object.random=childRandom(random);
    auto &rule=object.rule;
    if(const auto mode=ports_.quests.initialObjectMode(object.area,object.id,object.definition,rule.operation,rule.initialization,tick))
        object.mode=*mode;
    if(rule.initialization==1) {
        const auto &area=ports_.areas.at(object.area).definition;
        const auto &entries=area.shrineGeneration;
        if(entries.size()<2) throw std::logic_error("Missing native shrine generation table");
        int shrineClass=0;
        if(rule.parameters[0]) {
            shrineClass=rule.parameters[0]==1?2:rule.parameters[0]==2?3:4;
            if(shrineClass==4 && limitedRandom(controlRandom,10)==0) shrineClass=1;
        }
        std::vector<size_t> candidates;
        if(shrineClass) {
            for(size_t index=0;index<entries.size();++index)
                if(entries[index].effectClass==shrineClass) candidates.push_back(index);
            if(candidates.empty()) throw std::logic_error("Missing native shrine effect class");
        }
        size_t selected=0;
        for(int attempt=0;attempt<8;++attempt) {
            selected=shrineClass?candidates[limitedRandom(controlRandom,uint32_t(candidates.size()))]
                :1+limitedRandom(controlRandom,uint32_t(entries.size()-1));
            if(!selected) selected=1;
            if(int(object.area)>=entries[selected].minimumLevel) break;
        }
        int code=entries[selected].code;
        code=code==4?2:code==5?3:code==16?18:code;
        rule.shrine=area.shrineRules.at(code);
    }
    if(rule.initialization==3 || rule.initialization==57) {
        const bool sparkly=rule.initialization==57 || (rule.chest && rule.chest->sparkly);
        rule.chest.emplace();rule.chest->lockable=rule.lockable;rule.chest->sparkly=sparkly;
        resetChestRandom(*rule.chest,rule.initializationLevel,controlRandom);
        object.random=rule.chest->lootSeed;
    } else if(rule.initialization==2) {
        rule.trap=limitedRandom(controlRandom,100)<unsigned(rule.initializationLevel/8+5)
            ? 1+int(limitedRandom(controlRandom,8)):0;
    } else if(rule.initialization==27) {
        rule.trap=limitedRandom(controlRandom,1000)<333?3:0;
    }
    if(rule.initialization==8) object.mode=2;
    if(rule.initialization==12 && !object.mode) {object.mode=1;object.until=tick+rule.openingTicks;}
    if(rule.initialization==11 && object.mode==1) object.until=tick+rule.openingTicks;
    if(rule.initialization==11) object.portalDestination=uint8_t(ports_.areas.at(object.area).definition.townRegion);
    if(rule.initialization==12) {
        const auto &area=ports_.areas.at(object.area).definition;
        switch(int(object.area)) {
        case 1:object.portalDestination=39;break;
        case 39:object.portalDestination=1;break;
        case 38:object.portalDestination=4;break;
        case 4:object.portalDestination=38;break;
        case 74:object.portalDestination=46;break;
        case 46:object.portalDestination=74;break;
        case 73:object.portalDestination=uint8_t(area.staffTomb.value_or(0));break;
        case 121:object.portalDestination=109;break;
        case 109:object.portalDestination=121;break;
        case 125:object.portalDestination=111;break;
        case 126:object.portalDestination=112;break;
        case 127:object.portalDestination=117;break;
        case 111:object.portalDestination=125;object.portalAt=tick+1;break;
        case 112:object.portalDestination=126;object.portalAt=tick+1;break;
        case 117:object.portalDestination=127;object.portalAt=tick+1;break;
        default:if(area.staffTomb && int(object.area)==*area.staffTomb) object.portalDestination=73;break;
        }
    }
    if(rule.initialization==14 || rule.initialization==26) object.mode=1;
    if(rule.initialization==16) object.uses=int(uint8_t(2*uint8_t(rule.parameters[2])));
    if(rule.initialization==17) {
        if(waypointArrival(object)) {
            if(!object.mode) {object.mode=1;object.until=tick+rule.openingTicks-1;}
        } else if(ports_.areas.at(object.area).definition.town) object.mode=2;
    }
    if(object.rule.initialization==22 || object.rule.initialization==24) object.hazardAt=tick+25;
    if(object.rule.initialization==22 && !object.mode) object.mode=object.rule.initialMode;
    if(object.rule.initialization==34 && limitedRandom(controlRandom,2)) object.mode=1;
    if(object.rule.initialization==51 && (object.mode==1 || object.mode==2)) object.hazardAt=tick+35;
    if(object.rule.initialization==58) object.hazardAt=tick+25+limitedRandom(controlRandom,250);
    if(rule.initialization==59 && !object.mode) {
        object.mode=1;object.hazardAt=tick+27;object.until=tick+rule.openingTicks;
    }
}
void System::finishInitialization(Object &object,uint64_t &controlRandom) {
    auto &rule=object.rule;
    if(rule.preOperate && limitedRandom(controlRandom,14)==0) object.mode=2;
    if(rule.shrinePreset>=574 && rule.shrinePreset<=579) {
        constexpr int minimum[]{2,7,8,12,1,14},maximum[]{6,7,11,12,5,14};
        const auto index=size_t(rule.shrinePreset-574);
        int code=minimum[index]+int(limitedRandom(object.random,unsigned(maximum[index]-minimum[index])));
        if(code==4 || code==5) code=2;
        rule.shrine=ports_.areas.at(object.area).definition.shrineRules.at(code);
    }
    if(rule.specialChest) {
        const bool sparkly=int(object.area)<62 || int(object.area)>64 || limitedRandom(controlRandom,100)<=50;
        if(rule.chest) rule.chest->sparkly=sparkly;
        if(sparkly && int(object.area)==25) {
            rule.trap=3;
            if(rule.chest) {rule.chest->trap=3;rule.chest->locked=false;}
        }
        object.mode=0;
    }
    object.sparkChest=rule.chest && rule.chest->sparkly?1:0;
}
DomainResult<> System::prepareGoldPlaceholder(Object &object,uint64_t &random,uint64_t &controlRandom,EntityIds &ids,
    std::vector<std::pair<items::PreparedBatch,GroundLocation>> &gold,std::vector<EntityId> &identities,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &sample) {
    if(object.rule.initialization!=28 || object.mode) return {DomainStatus::Applied,std::monostate{}};
    object.mode=2;
    const auto &area=ports_.areas.at(object.area).definition;
    const auto *bounds=area.activation.room(object.position);
    if(!bounds) return {DomainStatus::Unavailable,{}};
    const RoomView room{object.area,(int(area.origin.x)+bounds->x)/5,(int(area.origin.y)+bounds->y)/5};
    const int x=int(object.position.x+area.origin.x),y=int(object.position.y+area.origin.y);
    const auto collision=[&](RoomView view,int32_t column,int32_t row) {
        auto mask=sample(view,column,row);
        const auto &origin=ports_.areas.at(view.area).definition.origin;
        for(const auto &[prepared,location]:gold) {
            (void)prepared;
            if(location.region==view.area && int(location.position.x+origin.x)==column && int(location.position.y+origin.y)==row) mask|=0x200;
        }
        return mask;
    };
    const int count=1+int(limitedRandom(controlRandom,9));
    for(int index=0;index<count;++index) {
        const int column=nativeAdd(x,int(rollRandom(controlRandom)&3));
        const int row=nativeAdd(y,int(rollRandom(controlRandom)&3));
        const auto owner=ports_.areas.roomAtPosition(object.area,object.position,column,row);
        if(!owner || owner->area!=room.area || owner->x!=room.x || owner->y!=room.y || (collision(room,column,row)&0x3f11)) continue;
        const auto point=ports_.areas.nativeFreeSpace(room,column,row,1,collision);
        if(!point) continue;
        if(ports_.items.read().world.items.size()+gold.size()>=4096 || ids.cursor()>UINT32_MAX) return {DomainStatus::Capacity,{}};
        auto prepared=ports_.items.prepareNativeGold(unsigned(std::max(area.monsterLevel-1,1)),random,false);
        if(!prepared) return {prepared.status,{}};
        identities.push_back(ids.allocate());
        gold.emplace_back(std::move(*prepared.value),GroundLocation{point->room.area,point->position});
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::prepareSoulPlaceholder(Object &object,uint64_t tick,uint64_t &random,uint64_t &controlRandom,EntityIds &ids,
    monsters::NativeBirthBatch &births,std::optional<RoomRandomLoan> &roomRandom,const std::function<uint16_t(RoomView,int32_t,int32_t)> &sample) {
    if(object.rule.initialization!=46 || object.mode) return {DomainStatus::Applied,std::monostate{}};
    const auto &area=ports_.areas.at(object.area).definition;
    const auto *bounds=area.activation.room(object.position);
    if(!bounds) return {DomainStatus::Unavailable,{}};
    const int left=int(area.origin.x)+bounds->x,top=int(area.origin.y)+bounds->y;
    const RoomView room{object.area,left/5,top/5};
    if(!roomRandom) {
        auto loan=ports_.areas.roomRandom(room);if(!loan) return {loan.status,{}};
        roomRandom=*loan.value;
    }
    const auto collision=[&](RoomView view,int32_t column,int32_t row) {
        auto mask=sample(view,column,row);
        const auto &origin=ports_.areas.at(view.area).definition.origin;
        for(const auto &[id,actor]:births.actors) {
            (void)id;
            if(actor.area!=view.area) continue;
            mask|=nativeCollisionFootprint(actor.collisionPattern.value_or(actor.rule.collisionPattern),
                column-int(actor.position.x+origin.x),row-int(actor.position.y+origin.y),0x100);
        }
        return mask;
    };
    const auto clear=[&](int column,int row,int width,uint16_t mask) {
        for(int y=row-width/2;y<row-width/2+width;++y) for(int x=column-width/2;x<column-width/2+width;++x) {
            const auto cell=ports_.areas.roomAtPosition(object.area,object.position,x,y);
            if(!cell || (collision(*cell,x,y)&mask)) return false;
        }
        return true;
    };
    const auto eligible=[&](int column,int row) {
        const int checkedX=int(uint16_t(column)),checkedY=int(uint16_t(row));
        return bounds->width>=5 && bounds->height>=5 && checkedX>left+1 && checkedY>top+1 &&
            checkedX<left+bounds->width-5 && checkedY<top+bounds->height-5 &&
            clear(column,row,10,0x0c01) && clear(column,row,3,0x3f11);
    };
    const auto spawn=[&](Vec position)->DomainResult<EntityId> {
        const auto code=limitedRandom(controlRandom,2)?"trappedsoul2":"trappedsoul1";
        const auto prototype=area.environmentMonsters.find(code);
        if(prototype==area.environmentMonsters.end() || prototype->second.rule.trap!=MonsterRule::Trap::Soul)
            return {DomainStatus::NotImplemented,{}};
        auto identity=prototype->second.identity;
        identity.spawnKey="object.soul."+std::to_string(object.id.value)+"."+std::to_string(ids.cursor());
        monsters::Admission request{identity,prototype->second.implementation,object.area,position,true,prototype->second.rule,{},tick};
        return ports_.monsters.stageRoomSpawn(request,*roomRandom,-1,ids,random,births,collision);
    };
    int remaining=(30*((bounds->width*bounds->height)>>7))>>8;
    constexpr int horizontal[]{-1,0,1,-1,1,-1,0,1};
    constexpr int vertical[]{-1,-1,-1,0,0,1,1,1};
    for(int attempt=0;attempt<12 && remaining>0;++attempt) {
        int column=left+int(limitedRandom(controlRandom,uint32_t(bounds->width-4)));
        int row=top+int(limitedRandom(controlRandom,uint32_t(bounds->height-4)));
        if(!eligible(column,row)) continue;
        auto born=spawn(object.position);if(!born) return {born.status,{}};
        if(*born.value) {object.mode=2;--remaining;}
        int clusterSize=1;
        bool found=true;
        while(limitedRandom(controlRandom,uint32_t(clusterSize>>1))==0 && found) {
            found=false;
            const int searches=3*std::max(remaining,4);
            for(int search=0;search<searches && !found;++search) {
                const auto direction=limitedRandom(controlRandom,8);
                column=nativeAdd(column,2*(5+int(limitedRandom(controlRandom,5)))*horizontal[direction]);
                row=nativeAdd(row,2*(5+int(limitedRandom(controlRandom,5)))*vertical[direction]);
                found=eligible(column,row);
            }
            if(!found) continue;
            ++clusterSize;
            born=spawn(Vec{float(column),float(row)}-area.origin);if(!born) return {born.status,{}};
            if(*born.value) {object.mode=2;--remaining;}
        }
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<EntityId> System::admit(const Admission &request) {
    const auto *area=ports_.areas.find(request.area);
    if(!area || !std::isfinite(request.position.x) || !std::isfinite(request.position.y) || request.mode<0 || request.mode>7) return {DomainStatus::InvalidRequest,{}};
    const auto source=area->definition.objectRules.find(request.definition);
    if(source==area->definition.objectRules.end() || (request.position.x<0 || request.position.y<0 || request.position.x>=area->definition.collision.width || request.position.y>=area->definition.collision.height)) return {DomainStatus::InvalidRequest,{}};
    if(!request.room && !area->definition.activation.room(request.position)) return {DomainStatus::Unavailable,{}};
    if(request.room) {
        if(request.room->area!=request.area) return {DomainStatus::InvalidRequest,{}};
        const auto &rooms=area->definition.activation.rooms();
        const auto found=std::find_if(rooms.begin(),rooms.end(),[&](const auto &bounds) {
            return (int(area->definition.origin.x)+bounds.x)/5==request.room->x &&
                (int(area->definition.origin.y)+bounds.y)/5==request.room->y;
        });
        if(found==rooms.end() || size_t(found-rooms.begin())>=area->rooms.size() ||
            !area->rooms[size_t(found-rooms.begin())].loaded) return {DomainStatus::Unavailable,{}};
    }
    auto rule=source->second;
    if(request.shrine) {
        const int code=*request.shrine==4?2:*request.shrine==5?3:*request.shrine==16?18:*request.shrine;
        const auto shrine=area->definition.shrineRules.find(code);
        if(rule.operation!=2 || shrine==area->definition.shrineRules.end() || *request.shrine<1) return {DomainStatus::InvalidRequest,{}};
        rule.shrine=shrine->second;
    }
    if(request.trap) {
        if(*request.trap<0 || *request.trap>9 || rule.operation!=4) return {DomainStatus::InvalidRequest,{}};
        rule.chest.emplace();rule.chest->trap=*request.trap;
    }
    if(state_.objects.size()>=65536 || ports_.ids.cursor()>=UINT32_MAX) return {DomainStatus::Capacity,{}};
    auto ids=ports_.ids;
    const auto id=ids.allocate();Object object{id,request.definition,request.area,request.position,1,request.mode,std::move(rule)};
    object.pd2Binding=request.pd2Binding;object.collisionReleased=request.collisionReleased;object.nativePortalOwner=request.nativePortalOwner;object.pd2Portal=request.pd2Portal;
    auto random=ports_.random;
    auto controlRandom=state_.controlRandom.value();
    object.uses=2*object.rule.parameters[2];initialize(object,request.tick,random,controlRandom);
    std::vector<std::pair<items::PreparedBatch,GroundLocation>> gold;
    std::vector<EntityId> goldIds;
    const auto initialized=prepareGoldPlaceholder(object,random,controlRandom,ids,gold,goldIds,ports_.spatial.nativeCollisionSample());
    if(!initialized) return {initialized.status,{}};
    monsters::NativeBirthBatch births;
    std::optional<RoomRandomLoan> soulRandom;
    const auto souls=prepareSoulPlaceholder(object,request.tick,random,controlRandom,ids,births,soulRandom,ports_.spatial.nativeCollisionSample());
    if(!souls) return {souls.status,{}};
    auto actors=ports_.monsters.prepareActorPublication(std::move(births.actors),size_t(ids.cursor()-ports_.ids.cursor()));
    auto room=ports_.areas.prepareRoomUnit(id,2,object.area,object.position,request.room);
    const auto installed=ports_.items.installNativeGroundBatch(gold,request.tick,goldIds,actors.identities);
    if(!installed) return {installed.status,{}};
    if(object.rule.initialization==79) {
        const auto zoo=ports_.quests.initializeZoo(request.area,request.tick);
        if(!zoo) return {zoo.status,{}};
    }
    finishInitialization(object,controlRandom);
    if(request.shrine) {
        const int code=*request.shrine==4?2:*request.shrine==5?3:*request.shrine==16?18:*request.shrine;
        object.rule.shrine=area->definition.shrineRules.at(code);
    }
    if(request.trap && object.rule.chest) object.rule.chest->trap=uint8_t(*request.trap);
    state_.objects.emplace(id,std::move(object));
    publishInitialization(state_.objects.at(id));
    ports_.monsters.publishBirthComponents(births);ports_.monsters.publishActors(actors);
    ports_.areas.publishRoomUnit(*room);
    if(soulRandom) ports_.areas.publishRoomRandom(*soulRandom);
    ports_.random=random;
    state_.controlRandom=controlRandom;
    collision(request.area);
    return {DomainStatus::Applied,id};
}
void System::compressRoom(RegionId region,size_t index,uint64_t tick) {
    const auto &area=ports_.areas.at(region).definition;
    const auto &bounds=area.activation.rooms().at(index);
    const RoomView room{region,(int(area.origin.x)+bounds.x)/5,(int(area.origin.y)+bounds.y)/5};
    auto &saved=inactive_[{region,index}];
    for(const auto id:ports_.areas.roomUnitOrder(room)) {
        const auto found=state_.objects.find(id);if(found==state_.objects.end()) continue;
        const auto &object=found->second;const auto &rule=object.rule;
        EntityId preset;
        for(const auto &[source,current]:presetObjects_) if(current==id) {preset=source;break;}
        if((area.saveMonsters || object.resurrected) && rule.restore && !(object.sparkChest&2) && !(rule.restoreVirgins && object.mode!=0)) {
            int mode=object.mode;
            if(mode==1 && !rule.cycleOperating && rule.openedMode) mode=2;
            const int trap=rule.chest?int(rule.chest->trap):rule.trap;
            saved.insert(saved.begin(),{id,preset,region,object.position,object.definition,mode,object.uses,trap,
                rule.shrine?rule.shrine->code:0,tick,object.reset,rule.chest && rule.chest->locked,object.sparkChest,object.portalDestination,
                object.pd2Binding,object.collisionReleased,object.nativePortalOwner,object.pd2Portal});
        }
        ports_.frameEvents.cancelOwner({FrameEventUnit::Object,id});
        ports_.areas.removeRoomUnit(id);
        std::erase_if(approaches_,[&](const auto &entry){return entry.second.request.target.id==id;});
        state_.objects.erase(found);
    }
    if(saved.empty()) inactive_.erase({region,index});
    collision(region);
}
StepStatus System::restoreRoom(RegionId region,size_t index,uint64_t tick) {
    const auto found=inactive_.find({region,index});if(found==inactive_.end()) return StepStatus::Complete;
    auto &saved=found->second;
    while(!saved.empty()) {
        const auto &source=saved.front();
        if(std::any_of(state_.objects.begin(),state_.objects.end(),[&](const auto &entry) {
            return entry.second.nativePortalOwner==source.previous && entry.second.revision==UINT64_MAX;
        })) return StepStatus::Blocked;
        const auto restored=admit({source.definition,region,source.position,{},{},source.mode,tick,{},source.pd2Binding,source.collisionReleased,source.nativePortalOwner,source.pd2Portal});
        if(!restored) return isDeferred(restored.status) || restored.status==DomainStatus::Conflict || restored.status==DomainStatus::Stale?
            StepStatus::Blocked:StepStatus::NotImplemented;
        const auto id=*restored.value;auto &object=state_.objects.at(id);
        restoreNativePortalReferences(source.previous,id);
        object.resurrected=true;
        if((object.rule.subClass&1) && source.frame) {
            if(source.shrine) object.rule.shrine=ports_.areas.at(region).definition.shrineRules.at(source.shrine);
            object.reset=source.reset?std::max(source.reset,tick+1):0;
        } else if(!(object.rule.subClass&0x20)) {
            object.trap=source.trap;
            object.rule.trap=source.trap;
            object.sparkChest=source.sparkChest;
            object.portalDestination=source.portalDestination;
            if(object.rule.chest) {object.rule.chest->trap=source.trap;object.rule.chest->locked=source.locked;object.rule.chest->sparkly=(source.sparkChest&1)!=0;}
        } else {
            const int maximum=2*object.rule.parameters[2],interval=object.rule.parameters[0];
            object.uses=source.uses;
            if(object.uses<maximum && interval>0) {
                const auto elapsed=uint32_t(tick)-uint32_t(source.frame);
                const auto savedFrame=int32_t(uint32_t(source.frame));
                const auto nextFrame=int32_t(uint32_t(tick)+uint32_t(interval));
                if(savedFrame<nextFrame) {
                    const int recovered=std::min(nativeNarrow(elapsed/uint32_t(interval)),1);
                    object.uses=std::min(maximum,int(uint8_t(source.uses+recovered)));
                    if(object.rule.parameters[2]>0) object.mode=2-object.uses/object.rule.parameters[2];
                    if(object.mode<0 || object.mode>2) {object.mode=0;object.uses=maximum;}
                }
                for(int charge=object.uses;charge<maximum;++charge)
                    ports_.frameEvents.insertPrepared({{},{FrameEventUnit::Object,id},FrameEventType::AiThink,
                        tick+uint64_t(interval)*uint64_t(charge-object.uses+1)});
            }
        }
        if(source.preset) {
            admittedPresets_.erase(source.preset);admittedPresets_.insert(id);
            presetObjects_.erase(source.preset);presetObjects_.emplace(id,id);
            ports_.world.rebindObject(region,source.preset,id);
        }
        ports_.quests.rebindObject(source.previous,id);
        for(auto &chest:towerChests_) if(chest==source.previous) chest=id;
        ++object.revision;collision(region);
        saved.erase(saved.begin());
    }
    inactive_.erase(found);return StepStatus::Complete;
}
DomainResult<> System::damage(Object &object,DamageType type,bool playersOnly,int radius,TickContext tick,std::optional<EntityId> only) {
    const auto &area=ports_.areas.at(object.area);if(area.definition.town) return {DomainStatus::Applied,std::monostate{}};
    const bool roomTargets=object.rule.initialization==22 || object.rule.initialization==24 || object.rule.initialization==51;
    auto random=object.random;
    combat::SpellImpact impact{object.id,object.id,object.area,type,0,{}};impact.environment=true;impact.environmentGeneration=area.generation;impact.occurrence=object.revision*8+uint64_t(type);
    impact.nativeObjectDamage=true;
    impact.hitClass=type==DamageType::Fire?0x20:type==DamageType::Lightning?0x40:type==DamageType::Cold?0x30:0;
    auto add=[&](EntityId id,Vec at,int64_t life,int level,int dexterity,int defense) {
        if(only && id!=*only) return;
        const int dx=int(at.x)-int(object.position.x),dy=int(at.y)-int(object.position.y);
        const bool inRange=roomTargets?unitDistanceWithSizes(int(std::floor(object.position.x+.5f)),int(std::floor(object.position.y+.5f)),object.rule.width,
            int(std::floor(at.x+.5f)),int(std::floor(at.y+.5f)),2)<=radius:dx*dx+dy*dy<=radius*radius;
        if(life<=0 || !inRange || !area.definition.collision.interactionSegment(at,object.position,std::max(object.rule.width,object.rule.height),object.id)) return;
        const int parameter=nativeMultiply(2,nativeSubtract(nativeSubtract(int(uint8_t(uint32_t(level)+limitedRandom(random,unsigned(level>>2)))),
            nativeMultiply(5,dexterity>>1)),level));
        if(int(limitedRandom(random,100))>=std::max(nativeAdd(nativeSubtract(parameter,defense),125),65)) return;
        const int minimum=std::max(nativeNarrow(life)>>5,1),maximum=std::max(nativeNarrow(life)>>3,nativeAdd(minimum,1));
        const auto rolled=uint32_t(minimum)+limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(maximum,minimum),256)));
        const auto amount=(rolled*uint32_t(uint8_t(object.rule.damage)))/100u;
        if(amount>0) {impact.targets.push_back(id);impact.targetDamage.push_back(amount);}
    };
    if(roomTargets) {
        const auto *bounds=area.definition.activation.room(object.position);
        if(!bounds) return {DomainStatus::Unavailable,{}};
        const RoomView room{object.area,(int(area.definition.origin.x)+bounds->x)/5,(int(area.definition.origin.y)+bounds->y)/5};
        auto execution=damageExecutions_.find(object.id);
        if(execution==damageExecutions_.end()) {
            if(nextDamageOccurrence_==UINT64_MAX) return {DomainStatus::Capacity,{}};
            DamageExecution candidate;candidate.occurrence=nextDamageOccurrence_++;
            for(const auto id:ports_.areas.roomUnitOrder(room))
                if(const auto *player=ports_.players.findActor(id);player && player->entered) candidate.targets.push_back(id);
            execution=damageExecutions_.emplace(object.id,std::move(candidate)).first;
        }
        auto &cursor=execution->second;
        while(cursor.next<cursor.targets.size()) {
            if(!cursor.queued) {
                impact.targets.clear();impact.targetDamage.clear();random=object.random;
                const auto *player=ports_.players.findActor(cursor.targets[cursor.next]);
                if(player && player->entered && player->area==object.area)
                    add(player->actor,player->position,int64_t(player->persistent.player.hp.raw()),player->persistent.player.level,
                        player->totals.character.dexterity,player->totals.character.defense);
                impact.occurrence=cursor.occurrence;impact.synchronous=true;
                const auto admitted=ports_.combat.enqueue(impact);if(!admitted) return admitted;
                object.random=random;cursor.queued=true;
            }
            if(ports_.combat.advanceEnvironmentImpact(object.id,cursor.occurrence,tick)==StepStatus::Blocked)
                return {DomainStatus::Capacity,{}};
            cursor.queued=false;++cursor.next;
        }
        damageExecutions_.erase(execution);
        return {DomainStatus::Applied,std::monostate{}};
    } else for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==object.area) add(p.actor,p.position,int64_t(p.persistent.player.hp.raw()),p.persistent.player.level,p.totals.character.dexterity,p.totals.character.defense);}
    if(!playersOnly) for(const auto &[id,m]:ports_.monsters.read().actors) if(m.area==object.area && m.damageable()) add(id,m.position,m.life,m.rule.level,m.hireling?m.hirelingDexterity:0,ports_.effects.unitDefense(id,tick.tick));
    const auto result=ports_.combat.enqueue(std::move(impact));if(result) object.random=random;return result;
}
DomainResult<> System::explode(Object &object,const ActorContext &actor) {
    // Plan the entire connected barrel chain before changing any mode or RNG.
    std::vector<EntityId> chain{object.id};std::set<EntityId> seen{object.id};
    if(object.rule.operation==7) for(size_t i=0;i<chain.size();++i) {
        const auto &current=state_.objects.at(chain[i]);
        for(const auto &[id,other]:state_.objects) {
            if(other.area!=object.area || other.definition!=11 || other.mode || other.pending || seen.contains(id)) continue;
            const int dx=int(current.position.x)-int(other.position.x),dy=int(current.position.y)-int(other.position.y);
            if(dx*dx+dy*dy<=4) {seen.insert(id);chain.push_back(id);}
        }
    }
    // Each barrel has independent native damage rolls. Capacity is checked for
    // all impacts up front so a retry cannot re-explode a committed predecessor.
    if(ports_.combat.read().spells.size()+chain.size()*2>256 || ports_.combat.read().spellTargets+chain.size()*(ports_.players.all().size()+ports_.monsters.read().actors.size())*2>65536) return {DomainStatus::Capacity,{}};
    for(const auto id:chain) {
        auto &current=state_.objects.at(id);
        auto result=damage(current,DamageType::Physical,false,3,{actor.tick},current.rule.operation==30?std::optional{actor.actor}:std::nullopt);if(!result) return result;
        if(current.rule.operation==30) {result=damage(current,DamageType::Fire,true,3,{actor.tick},actor.actor);if(!result) return result;}
        current.mode=1;current.until=actor.tick+std::max(uint64_t{1},current.rule.openingTicks);++current.revision;
    }
    collision(object.area);return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::hazardStep(TickContext tick,EntityId only) {
    bool blocked=false;std::set<RegionId> changed;
    for(auto entry=state_.objects.find(only);entry!=state_.objects.end() && entry->first==only;++entry) {
        auto &[id,object]=*entry;
        if (!ports_.areas.loaded(object.area, object.position)) continue;
        (void)id;if(!object.hazardAt || tick.tick<object.hazardAt) continue;
        bool occupied=false;
        const int radius=object.rule.initialization==24?0:object.rule.parameters[0]+1;
        const auto &definition=ports_.areas.at(object.area).definition;
        const auto *bounds=definition.activation.room(object.position);if(!bounds) continue;
        const RoomView room{object.area,(int(definition.origin.x)+bounds->x)/5,(int(definition.origin.y)+bounds->y)/5};
        for(const auto id:ports_.areas.roomUnitOrder(room)) {
            const auto *player=ports_.players.findActor(id);
            if(!player || !player->entered || player->persistent.player.hp<=0) continue;
            if(unitDistanceWithSizes(int(std::floor(object.position.x+.5f)),int(std::floor(object.position.y+.5f)),object.rule.width,
                int(std::floor(player->position.x+.5f)),int(std::floor(player->position.y+.5f)),2)<=radius) occupied=true;
        }
        if(object.rule.initialization==51) {
            if(object.mode==1) object.hazardAt=tick.tick+10;
            else if(object.mode==2 && !object.phase) {
                if(occupied || damageExecutions_.contains(object.id)) {const auto result=damage(object,DamageType::Fire,true,radius,tick);if(!result) {blocked|=isDeferred(result.status);continue;}}
                object.phase=1;object.hazardAt=tick.tick+25;
            } else if(object.mode==2 && !object.pending) {
                const PlayerState *player=ports_.players.find(object.operatorId);
                if(!player) for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==object.area) {player=&p;break;}}
                if(!player) continue;
                LootRequest source;source.source=object.id;source.region=object.area;source.difficulty=ports_.settings.difficulty;
                const auto result=ports_.loot.queue({object.revision,source,player->player,object.position,loot::ObjectSource{object.definition,48,{},{}},{}});
                if(result) {object.pending=true;object.hazardAt=0;}else blocked|=isDeferred(result.status);
            } else if(object.mode==3) {object.mode=4;++object.revision;changed.insert(object.area);object.hazardAt=0;}
            else object.hazardAt=0;
        } else if(object.rule.initialization==58) {
            object.hazardAt=0;
            if(object.mode==0 || object.mode==2) {
                object.hazardAt=tick.tick+25+limitedRandom(*state_.controlRandom,250);
                object.mode=1;object.until=tick.tick+std::max(uint64_t{1},object.rule.openingTicks);++object.revision;changed.insert(object.area);
            }
        } else if(object.rule.initialization==22) {
            if(occupied || damageExecutions_.contains(object.id)) {const auto result=damage(object,DamageType::Fire,true,radius,tick);if(!result) {blocked|=isDeferred(result.status);continue;}}
            if(object.mode==1) {object.mode=2;++object.revision;changed.insert(object.area);}
            object.hazardAt=tick.tick+15+limitedRandom(*state_.controlRandom,35);
        } else if(object.rule.initialization==24) {
            if(object.phase==0) {
                if(occupied) {if(!ports_.events.hasCapacity(1)) {blocked=true;continue;}const auto result=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},object.area},{SoundFact{object.id,2,object.area,13}}});if(!result) {blocked=true;continue;}}
                object.phase=occupied?1:0;object.hazardAt=tick.tick+(occupied?25:15);}
            else if(object.phase==1) {
                if(occupied || damageExecutions_.contains(object.id)) {const auto result=damage(object,DamageType::Physical,true,0,tick);if(!result) {blocked|=isDeferred(result.status);continue;}}
                object.phase=occupied?2:0;object.mode=occupied?1:0;++object.revision;changed.insert(object.area);object.hazardAt=tick.tick+std::max(uint64_t{1},object.rule.openingTicks);
            } else {
                if(object.phase==2 && object.mode) {object.phase=0;object.mode=0;++object.revision;changed.insert(object.area);}
                object.hazardAt=tick.tick+15;
            }
        }
    }
    for(const auto area:changed) collision(area);
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
