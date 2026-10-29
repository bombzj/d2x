#include "system.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "server/player_store.hpp"
#include "server/movement.hpp"
#include "server/area_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/quest/catalog.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::objects {
void System::onWaypointArrival(RegionId area,Vec position) {
    waypointArrivals_.insert(waypointArrivals_.begin(),{area,position});
}
bool System::waypointArrival(const Object &object) const {
    if(object.rule.initialization!=17) return false;
    const auto &definition=ports_.areas.at(object.area).definition;
    const auto *room=definition.activation.room(object.position);
    return room && std::any_of(waypointArrivals_.begin(),waypointArrivals_.end(),[&](const auto &entry) {
        return entry.first==object.area && definition.activation.room(entry.second)==room;
    });
}
void System::publishInitialization(const Object &object) {
    if(object.rule.chest || object.rule.shrine || object.rule.operation==18)
        publishedModes_.insert_or_assign(object.id,std::pair{object.revision,object.mode});
    ports_.maps.bind(object.pd2Binding,object.id,object.area,object.position,true);
    if(ports_.maps.opensBlockerOnBirth(object.definition)) openNativeBlocker(object.id,ports_.frameEvents.currentFrame());
    ports_.quests.registerCainObject(object.id,object.definition,object.rule.initialization,object.area,object.position);
    ports_.quests.registerNpcStoryObject(object.id,object.area,object.definition,object.rule.initialization,object.position);
    if(waypointArrival(object)) waypointArrivals_.clear();
    if(object.portalAt) ports_.world.retainControllerRoom(object.area,object.position,true);
}
DomainResult<bool> System::spawnTownCain(const AreaNpc &npc,RegionId region,Vec marker,uint64_t tick) {
    if(ports_.monsters.find(npc.id)) {ports_.quests.registerTownCain(npc.id);return {DomainStatus::Applied,true};}
    if(!npc.rule.combat) return {DomainStatus::NotImplemented,{}};
    if(ports_.monsters.read().actors.size()>=65536) return {DomainStatus::Capacity,{}};
    const auto &quest=ports_.quests.read().actOne;
    const auto control=state_.objects.find(quest.cainStartObject);
    if(control==state_.objects.end() || control->second.rule.initialization!=54 || control->second.area!=region)
        return {DomainStatus::Stale,{}};
    marker=control->second.position;
    const auto &area=ports_.areas.at(region).definition;
    const auto *bounds=area.activation.room(marker);if(!bounds) return {DomainStatus::Unavailable,{}};
    const RoomView initial{region,(int(area.origin.x)+bounds->x)/5,(int(area.origin.y)+bounds->y)/5};
    const auto original=marker+area.origin;
    int column=int(original.x),row=int(original.y),counter=0;
    while(column<int(area.origin.x)+bounds->x || row<int(area.origin.y)+bounds->y ||
        column>=int(area.origin.x)+bounds->x+bounds->width-1 || row>=int(area.origin.y)+bounds->y+bounds->height-1) {
        ++column;++row;
        if(++counter>20) {column=int(original.y);break;}
    }
    const auto collision=ports_.spatial.nativeCollisionSample();
    auto free=ports_.areas.nativeQuestFreePosition(initial,column,row,2,0x100,100,collision);
    auto room=free?free->room:initial;
    Vec point=free?free->position:marker;
    std::map<RoomView,RoomRandomLoan> loans;
    const auto attempt=[&](int count)->DomainResult<std::optional<Vec>> {
        if(room.area!=region) return {DomainStatus::NotImplemented,{}};
        auto loan=loans.find(room);
        if(loan==loans.end()) {
            const auto borrowed=ports_.areas.roomRandom(room);if(!borrowed) return {borrowed.status,{}};
            loan=loans.emplace(room,*borrowed.value).first;
        }
        monsters::Admission admission;admission.identity.monster=npc.rule.code;admission.identity.spawnKey="cain.start";
        admission.area=region;admission.position=point;admission.rule=*npc.rule.combat;admission.tick=tick;
        return ports_.monsters.searchRoomSpawn(admission,loan->second,count,collision);
    };
    auto placed=attempt(5);if(!placed) return {placed.status,{}};
    for(int retry=0;retry<20 && !*placed.value;++retry) {
        auto global=point+area.origin;global.x+=1;global.y+=1;
        const auto current=ports_.areas.roomAtPosition(region,Vec{float(room.x*5),float(room.y*5)}-area.origin,int(global.x),int(global.y));
        if(current) room=*current;
        else {room=initial;global=original;}
        free=ports_.areas.nativeQuestFreePosition(room,int(global.x),int(global.y),2,0x100,100,collision);
        room=free?free->room:initial;point=free?free->position:marker;
        placed=attempt(10);if(!placed) return {placed.status,{}};
    }
    if(!*placed.value) {room=initial;point=marker;placed=attempt(15);if(!placed) return {placed.status,{}};}
    for(const auto &[view,loan]:loans) {(void)view;if(!ports_.areas.validateRoomRandom(loan)) return {DomainStatus::Stale,{}};}
    if(*placed.value) {
        const auto admitted=ports_.monsters.admitNpc(npc.id,region,**placed.value,npc.rule);
        if(!admitted) return {admitted.status,{}};
        ports_.quests.registerTownCain(npc.id);
    }
    for(const auto &[view,loan]:loans) {(void)view;ports_.areas.publishRoomRandom(loan);}
    return {DomainStatus::Applied,placed.value->has_value()};
}
void System::collision(RegionId area,bool synchronizeTimers) {
    std::vector<Grid::Obstacle> values;
    for (auto &[id, object] : state_.objects) {
        if (object.area != area) continue;
        if(synchronizeTimers) schedule(object);
        const auto &rule = object.rule; const auto mode = size_t(object.mode);
        const bool statueReleased=rule.initialization>=63 && rule.initialization<=65 && (object.mode==3 || object.mode==4);
        const bool colliding=rule.collision[mode] && !statueReleased && !object.collisionReleased;
        if (rule.width <= 0 || rule.height <= 0 || !colliding) continue;
        values.push_back({id, int(object.position.x) - rule.width / 2, int(object.position.y) - rule.height / 2,
            rule.width, rule.height, rule.collisionMask, (rule.collisionMask & 0x22) != 0});
    }
    ports_.world.objectCollision(area, std::move(values));
}
std::pair<unsigned,unsigned> System::prisonerSpawnCounts() const {
    unsigned count{},completed{};
    for(const auto &group:prisonerGroups_) {
        if(group.attempts>=25 || group.spawned>=5) ++completed;
        count+=group.spawned;
    }
    return {count,completed};
}
StepStatus System::spawnCagedPrisoners(const AreaObject &source,RegionId region,uint64_t tick) {
    if(ports_.quests.read().initialFlags[questDefinition(QuestId::RescueOnMountArreat).nativeSlot]&0x8000)
        return StepStatus::Complete;
    auto group=std::find_if(prisonerGroups_.begin(),prisonerGroups_.end(),[&](const auto &entry) {
        return entry.position.x==source.position.x && entry.position.y==source.position.y;
    });
    if(group==prisonerGroups_.end()) {
        if(prisonerGroups_.size()>=3) return StepStatus::Complete;
        prisonerGroups_.push_back({source.position});group=std::prev(prisonerGroups_.end());
    }
    const auto &area=ports_.areas.at(region).definition;
    const auto *bounds=area.activation.room(source.position);if(!bounds) return StepStatus::NotImplemented;
    const RoomView room{region,(int(area.origin.x)+bounds->x)/5,(int(area.origin.y)+bounds->y)/5};
    for(const auto &npc:area.npcs) {
        if(npc.rule.questInitFunction!=62 || !npc.sourceObjectPosition || npc.sourceObjectClass!=source.type ||
            npc.sourceObjectPosition->x!=source.position.x || npc.sourceObjectPosition->y!=source.position.y ||
            admittedPresetNpcs_.contains(npc.id)) continue;
        if(!npc.rule.combat) return StepStatus::NotImplemented;
        while(group->attempts<25 && group->spawned<5) {
            if(ports_.monsters.read().actors.size()>=65536) return StepStatus::Blocked;
            auto loan=ports_.areas.roomRandom(room);if(!loan) return StepStatus::Blocked;
            monsters::Admission admission;admission.identity.monster=npc.rule.code;
            admission.identity.spawnKey="npc."+std::to_string(npc.id.value);
            admission.area=region;admission.position=source.position;admission.rule=*npc.rule.combat;admission.tick=tick;
            const auto placed=ports_.monsters.searchRoomSpawn(admission,*loan.value,5,ports_.spatial.nativeCollisionSample());
            if(!placed) return isDeferred(placed.status)?StepStatus::Blocked:StepStatus::NotImplemented;
            if(!ports_.areas.validateRoomRandom(*loan.value)) return StepStatus::Blocked;
            if(*placed.value) {
                const auto admitted=ports_.monsters.admitNpc(npc.id,region,**placed.value,npc.rule,true);
                if(!admitted) return isDeferred(admitted.status) || admitted.status==DomainStatus::Stale?StepStatus::Blocked:StepStatus::NotImplemented;
            }
            ports_.areas.publishRoomRandom(*loan.value);
            ++group->attempts;
            if(*placed.value) {++group->spawned;admittedPresetNpcs_.insert(npc.id);break;}
        }
        if(group->attempts>=25 || group->spawned>=5) break;
    }
    return StepStatus::Complete;
}
DomainResult<> System::openMonsterDoor(EntityId id,Vec destination,uint64_t tick) {
    const auto *monster=ports_.monsters.find(id);
    if(!monster || !monster->alive() || monster->life<=0 || monster->owner || !monster->rule.opensDoors) return {DomainStatus::InvalidActor,{}};
    const auto &grid=ports_.areas.at(monster->area).definition.collision;
    if(grid.collisionSegment(monster->position,destination,0x0800)) return {DomainStatus::Unavailable,{}};
    Object *nearest=nullptr;float distance=9;
    for(auto &[key,door]:state_.objects) {
        (void)key;if(door.area!=monster->area || !door.rule.door || !door.rule.monsterUsable || door.mode || door.pending || door.until>tick) continue;
        const auto delta=door.position-monster->position;const float candidate=delta.x*delta.x+delta.y*delta.y;
        if(candidate<distance) {distance=candidate;nearest=&door;}
    }
    if(!nearest || nearest->revision==UINT64_MAX) return {DomainStatus::Unavailable,{}};
    nearest->mode=2;nearest->until=tick+std::max(uint64_t{1},nearest->rule.openingTicks);++nearest->revision;collision(monster->area);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request, std::optional<int> remoteRange) {
    const auto *player = ports_.players.find(actor.player); const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    const auto found = state_.objects.find(request.target.id);
    if (!area || area->generation != actor.areaGeneration || found == state_.objects.end()) return {DomainStatus::Stale, {}};
    auto &object = found->second; const auto &rule = object.rule;
    if(request.action==Action::HighlightDoor) {
        // PlrMsg::Rcv0x3D and ObjMode::sub_6FC78BB0: hovering only
        // refreshes OPENED/SPECIAL3 occupancy, it never operates the object.
        const auto *objectArea=ports_.areas.find(object.area);
        if(!objectArea || objectArea->definition.act!=area->definition.act) return {DomainStatus::InvalidRequest,{}};
        const auto playerPoint=player->position+area->definition.origin;
        const auto objectPoint=object.position+objectArea->definition.origin;
        if(std::abs(int(playerPoint.x)-int(objectPoint.x))>10 || std::abs(int(playerPoint.y)-int(objectPoint.y))>10)
            return {DomainStatus::InvalidRequest,{}};
        if(object.mode!=2 && object.mode!=5) return {DomainStatus::Applied,std::monostate{}};
        const auto *bounds=objectArea->definition.activation.room(object.position);
        if(!bounds) return {DomainStatus::Unavailable,{}};
        const RoomView room{object.area,(int(objectArea->definition.origin.x)+bounds->x)/5,
            (int(objectArea->definition.origin.y)+bounds->y)/5};
        const auto sample=ports_.spatial.nativeCollisionSample(0x8180); // PLAYER | MONSTER | CORPSE.
        const int width=std::max(1,rule.width),height=std::max(1,rule.height);
        const int left=int(objectPoint.x)-width/2,bottom=int(objectPoint.y)-height/2;
        bool occupied=false;
        for(int y=bottom;y<bottom+height && !occupied;++y)
            for(int x=left;x<left+width;++x) if(sample(room,x,y)) {occupied=true;break;}
        const int mode=occupied?5:2;
        if(object.mode!=mode) {
            if(object.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
            object.mode=mode;++object.revision;collision(object.area);
        }
        return {DomainStatus::Applied,std::monostate{}};
    }
    // PlrMsg's UNIT_OBJECT path does not require the same level. Ordinary
    // doors on a level seam still use the actual object and the player's grid.
    // Other operators keep their current area-bound domain contracts.
    if(object.area!=actor.area && (!rule.door || rule.operation!=8)) return {DomainStatus::Stale,{}};
    const auto point=ports_.areas.linkedPosition(actor.area,object.area,object.position);
    if(!point) return {DomainStatus::Stale,{}};
    const InteractionTarget target{object.id, *point, *point, rule.width, rule.height, float(rule.range), true};
    if (!remoteRange && !interactionClear(area->definition.collision, player->position, target)) {
        if(nativeUnitDistance(player->position,2,*point,rule.width)>50) return {DomainStatus::InvalidRequest,{}};
        if(ports_.movement.execute(actor,{MovementAction::Move,*point,true,{},MotionTarget{object.id,2},target})!=CommandStatus::Applied)
            return {DomainStatus::Unavailable,{}};
        approaches_.insert_or_assign(actor.player,Approach{actor,request,player->locomotionSequence});
        return {DomainStatus::Applied,std::monostate{}};
    }
    if (remoteRange) {const int dx=int(point->x)-int(player->position.x),dy=int(point->y)-int(player->position.y);if(dx*dx+dy*dy>*remoteRange * *remoteRange) return {DomainStatus::InvalidRequest,{}};}
    if (object.pending || object.specialPending || object.revision == UINT64_MAX) return {DomainStatus::Conflict, {}};
    if(!remoteRange) ports_.movement.execute(actor,{MovementAction::Stop,{},false});
    if(rule.operation==15) return ports_.travel.useSpecial(actor,{travel::Kind::Portal,{object.id,object.revision,2},{}},remoteRange);
    if((rule.operation==77 || rule.operation==80) && object.nativePortalOwner)
        return ports_.travel.useNativeObjectPortal(actor,object.id,remoteRange);
    if(rule.operation==80 && object.definition==600) return ports_.maps.operateTome(actor,object.id);
    if(rule.operation==79) return ports_.maps.operateEvent(actor,object.id);
    if(rule.operation==78) return ports_.maps.operateAncients(actor,object.id);
    if(rule.operation==76) return ports_.maps.operateSacrifice(actor,object.id);
    if(rule.operation==23) {
        if(object.mode!=0 && object.mode!=1 && object.mode!=2) return {DomainStatus::Conflict,{}};
        // Native operation23 first activates and animates; only an operating
        // or opened waypoint offers the original0x63 menu.
        auto result=ports_.travel.openWaypoint(actor,object.id,object.mode!=0,remoteRange);
        if(result && object.mode==0) {object.mode=1;object.until=actor.tick+std::max(uint64_t{1},rule.openingTicks);++object.revision;collision(actor.area);}
        return result;
    }
    if (rule.stash) return ports_.inventory.openStash(actor,object.id,remoteRange);
    if(area->definition.act==4 && ((rule.operation>=62 && rule.operation<=67) || rule.operation==70 || rule.operation==72)) {
        if(rule.operation==65 && object.mode) return {DomainStatus::Conflict,{}};
        if((rule.operation==66 || rule.operation==70) && object.mode!=2) return {DomainStatus::Conflict,{}};
        const auto result=ports_.quests.operate(actor,object.id,object.definition,rule.operation,object.position);
        if(result) for(const auto &[player,pending]:ports_.quests.read().pending) {(void)player;if(pending.source==object.id && !pending.conversation) object.pending=true;}
        return result;
    }
    if(area->definition.act==3 && (rule.operation==49 || (object.definition>=392 && object.definition<=396))) {
        if(rule.operation==49?object.mode!=0 && object.mode!=2:object.mode!=0) return {DomainStatus::Conflict,{}};
        const auto result=ports_.quests.operate(actor,object.id,object.definition,rule.operation,object.position);
        if(result && object.definition>=392 && object.definition<=396) {
            object.mode=1;object.until=actor.tick+2*(rule.openingTicks-1);++object.revision;collision(actor.area);
        }
        if(result) for(const auto &[player,pending]:ports_.quests.read().pending) {(void)player;if(pending.source==object.id && !pending.conversation) object.pending=true;}
        return result;
    }
    if(area->definition.act==2 && (rule.operation==28 || rule.operation==31 || rule.operation==44 || rule.operation==45 || rule.operation==46 || rule.operation==53 || (rule.operation>=57 && rule.operation<=59))) {
        const bool travel=rule.operation==44 || rule.operation==46;
        if(travel?object.mode!=2:object.mode!=0) return {DomainStatus::Conflict,{}};
        const auto result=ports_.quests.operate(actor,object.id,object.definition,rule.operation,object.position);
        if(result) for(const auto &[player,pending]:ports_.quests.read().pending) {(void)player;if(pending.source==object.id && !pending.conversation) object.pending=true;}
        return result;
    }
    if(area->definition.act==1 && (rule.operation==24 || rule.operation==25 || rule.operation==34 || (rule.operation>=39 && rule.operation<=43))) {
        const bool reusable=rule.operation==25 || rule.operation==34 || rule.operation==42 || rule.operation==43;
        if(!reusable && object.mode) return {DomainStatus::Conflict,{}};
        if(rule.operation==43 && object.mode!=2) return {DomainStatus::Conflict,{}};
        const auto result=ports_.quests.operate(actor,object.id,object.definition,rule.operation,object.position);
        if(result && !reusable) object.pending=true;
        if(result && rule.operation==42 && !object.mode) {object.mode=1;object.until=actor.tick+std::max(uint64_t{1},rule.openingTicks);++object.revision;schedule(object);}
        return result;
    }
    if(rule.operation==6 || rule.operation==9 || rule.operation==10 || rule.operation==12 || (rule.operation==21 && object.definition==108)) {
        if(object.mode || object.pending) return {DomainStatus::Conflict,{}};
        Object *cainControl=nullptr;
        if(rule.operation==9) {
            const auto control=state_.objects.find(ports_.quests.read().actOne.cainInvisibleObject);
            if(control!=state_.objects.end() && control->second.definition==61) {
                if(control->second.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
                cainControl=&control->second;
            }
        }
        const auto result=ports_.quests.operate(actor,object.id,object.definition,rule.operation,object.position);
        if(result) {
            if(cainControl) {
                cainControl->mode=int(std::min(ports_.quests.read().actOne.activatedStones,5u))+1;
                cainControl->until=0;++cainControl->revision;
                if(cainControl->area!=actor.area) collision(cainControl->area);
            }
            if(rule.operation==12 || rule.operation==21) object.pending=true;
            else {object.mode=1;object.until=actor.tick+std::max(uint64_t{1},rule.openingTicks);++object.revision;collision(actor.area);}
        }
        return result;
    }
    if(rule.operation==18) {
        // ObjMode::OBJECTS_OperateFunction18_SecretDoor only opens neutral
        // objects, clears TARGETABLE and frees collision before ENDANIM.
        // This is not the reversible ordinary-door operation.
        if(object.mode) return {DomainStatus::Applied,std::monostate{}};
        object.operatorActor=actor.actor;
        return openNativeBlocker(object.id,actor.tick);
    }
    if (rule.door) {
        if (object.until > actor.tick) return {DomainStatus::Conflict, {}};
        if (object.mode) {
            const auto occupied = [&](RegionId region,Vec at) {
                const auto *other=ports_.areas.find(region),*source=ports_.areas.find(object.area);
                if(!other || !source || other->definition.act!=source->definition.act) return false;
                const auto local=at+other->definition.origin-source->definition.origin;
                if(std::abs(local.x-object.position.x)>rule.width/2.f+1 || std::abs(local.y-object.position.y)>rule.height/2.f+1) return false;
                return region==object.area || ports_.areas.linkedPosition(object.area,region,at).has_value();
            };
            for (const auto &[id, other] : ports_.players.all()) { (void)id; if (other.entered && other.persistent.player.hp > 0 && occupied(other.area,other.position)) return {DomainStatus::Conflict, {}}; }
            for (const auto &[id, other] : ports_.monsters.read().actors) { (void)id; if (other.alive() && occupied(other.area,other.position)) return {DomainStatus::Conflict, {}}; }
        }
        object.mode = object.mode ? 0 : 2; object.until = actor.tick + std::max(uint64_t{1},rule.openingTicks); ++object.revision; collision(object.area);
        return {DomainStatus::Applied, std::monostate{}};
    }
    if(rule.operation==11 || rule.operation==13) {
        object.mode=object.mode?0:1;object.until=0;++object.revision;collision(actor.area);return {DomainStatus::Applied,std::monostate{}};
    }
    if(rule.operation==7 || rule.operation==30) {if(object.mode) return {DomainStatus::Conflict,{}};return explode(object,actor);}
    if (object.mode && rule.operation != 22) return {DomainStatus::Conflict, {}};
    if (rule.shrine) {
        const auto result = shrine(actor, object); if (!result || object.pending) return result;
        object.operatorId=actor.player;object.operatorActor=actor.actor;
        object.mode = 1; object.until = !rule.cycleOperating && rule.openedMode ? actor.tick + std::max(uint64_t{1},rule.openingTicks) : 0; object.reset = rule.shrine->reset > 0 ? actor.tick + uint64_t(rule.shrine->reset)+1 : 0;
        ++object.revision; collision(actor.area); return result;
    }
    if (rule.operation == 22) {
        if (object.uses <= 0 || rule.parameters[2] <= 0 || rule.parameters[1] <= 0) return {DomainStatus::Conflict, {}};
        const auto &record = player->persistent.player; const auto &totals = player->totals.character;
        const auto restore = [&](Fixed8 current, Fixed8 maximum) {
            const auto cap = maximum.raw();
            if (current.raw() >= cap) return current;
            return Fixed8::fromRaw(std::min(cap,nativeAdd(current.raw(),nativeMultiply(cap,rule.parameters[1]) >> 8)));
        };
        const auto life = rule.parameters[3] & 2 ? restore(record.hp,totals.maxLifeFixed) : record.hp;
        const auto mana = rule.parameters[3] & 1 ? restore(record.mana,totals.maxManaFixed) : record.mana;
        const auto stamina = restore(record.stamina,totals.maxStaminaFixed);
        const auto result = ports_.effects.restoreWell(actor,life,mana,stamina,rule.cureStates); if (!result) return {result.status,{}};
        if(result.value->usesCharge) {
            --object.uses;
            if(object.uses<=2*rule.parameters[2] && object.uses%rule.parameters[2]==0) object.mode=2-object.uses/rule.parameters[2];
            if(rule.parameters[0]>0) ports_.frameEvents.insertPrepared({{},{FrameEventUnit::Object,object.id},
                FrameEventType::AiThink,actor.tick+uint64_t(rule.parameters[0])+1});
            ++object.revision; collision(actor.area);
        }
        return {DomainStatus::Applied,std::monostate{}};
    }
    if (rule.operation != 1 && rule.operation != 3 && rule.operation != 4 && rule.operation != 5 && rule.operation != 14 && rule.operation!=19 && rule.operation!=20 && rule.operation!=26 && rule.operation!=48 && rule.operation!=51 && rule.operation!=68) return {};
    if(rule.operation==48) {
        auto random=object.random;
        if(limitedRandom(random,100)>=90) {object.mode=1;object.until=actor.tick+std::max(uint64_t{1},rule.openingTicks);object.hazardAt=actor.tick+20;object.phase=0;object.operatorId=actor.player;object.random=random;++object.revision;collision(actor.area);return {DomainStatus::Applied,std::monostate{}};}
        object.random=random;
    }
    if(rule.operation==51) {
        auto candidate=object;candidate.trap=rule.trap;
        const auto scheduled=scheduleTrap(candidate,actor.tick);if(!scheduled) return scheduled;
        object=std::move(candidate);
        object.mode=1;object.until=actor.tick+std::max(uint64_t{1},rule.openingTicks);object.lootAt=actor.tick+uint64_t(std::max(0,rule.parameters[1]))+1;object.operatorId=actor.player;++object.revision;collision(actor.area);return {DomainStatus::Applied,std::monostate{}};
    }
    ItemHandle key;
    const bool lucionChest=object.definition==623 && area->definition.pd2Map && area->definition.pd2Map->controller==45;
    if (rule.chest && rule.chest->locked && player->definition.code != "ass" && !lucionChest) {
        for (const auto &[id, item] : player->persistent.inventory.items) {
            (void)id; const auto *at = std::get_if<ContainerLocation>(&item.location); const auto *definition = player->rules.items->find(item.definition);
            if (at && at->container == player->persistent.containers.backpack && item.quantity && definition && definition->equipment.isType("key")) { key = item.handle(); break; }
        }
        if (!key.id) {
            // OperateFunction04 handles a missing key without opening the chest.
            // The prompt belongs to its player, including deferred approaches.
            const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},
                {SoundFact{actor.actor,0,actor.area,22}}});
            if(!sent) return {sent.status,{}};
            return {DomainStatus::Applied,std::monostate{}};
        }
    }
    if(rule.operation==5 && !ports_.events.hasCapacity(1)) return {DomainStatus::Capacity,{}};
    LootRequest source; source.source = object.id; source.region = object.area; source.difficulty = ports_.settings.difficulty;
    const auto result = ports_.loot.queue({object.revision, source, actor.player, object.position, loot::ObjectSource{object.definition, rule.operation, lucionChest?std::nullopt:rule.chest, key},{}});
    if (result) {
        object.pending = true;object.operatorId=actor.player;
        if(rule.operation==5) {
            // Native ObjMode::OperateFunction05 starts KK independently of loot.
            // Original PlrModes can decline the action; this does not undo operation.
            ports_.skills.objectKick(actor,{object.id,object.revision,2});
        }
    }
    return result;
}
StepStatus System::populatePresets(RegionId requestedRegion,size_t requestedRoom,TickContext tick,std::optional<EntityId> only) {
    std::set<RegionId> changed;
    for(const auto &[region,area]:ports_.areas.all()) for(const auto &source:area.definition.objects) {
        if(only && source.id!=*only) continue;
        if(region!=requestedRegion || requestedRoom>=area.definition.activation.rooms().size() ||
            area.definition.activation.room(source.position)!=&area.definition.activation.rooms()[requestedRoom]) continue;
        if(!ports_.areas.loaded(region,source.position)) continue;
        if(source.rule.operation==23) {
            const auto global=source.position+area.definition.origin;
            const auto room=ports_.areas.roomAtPosition(region,source.position,int(global.x),int(global.y));
            if(room && ports_.travel.read().pd2SceneEntryRooms.contains(*room)) {
                admittedPresets_.insert(source.id);continue;
            }
        }
        if(int(region)==134) {
            auto decision=sandsPresetDecisions_.find(source.id);
            if(decision==sandsPresetDecisions_.end()) {
                // SUNIT's 1.13c Forgotten Sands object counter is evaluated
                // in actual room activation order, once per authored object.
                const bool omitted=++sandsPresetCount_==uint64_t(ports_.settings.mapSeed%3+3);
                decision=sandsPresetDecisions_.emplace(source.id,omitted).first;
            }
            if(decision->second) continue;
        }
        const auto admitChildren=[&] {
            if(source.rule.initialization==62) return spawnCagedPrisoners(source,region,tick.tick);
            for(const auto &npc:area.definition.npcs) {
                if(!npc.sourceObjectPosition || npc.sourceObjectClass!=source.type ||
                    npc.sourceObjectPosition->x!=source.position.x || npc.sourceObjectPosition->y!=source.position.y ||
                    admittedPresetNpcs_.contains(npc.id)) continue;
                if(npc.rule.questInitFunction==54) {
                    std::set<EntityId> townPlayers;
                    for(const auto &[id,player]:ports_.players.all()) {
                        (void)id;if(player.entered && player.area==region) townPlayers.insert(player.actor);
                    }
                    const bool requested=ports_.quests.cainTownRequested();
                    const bool entered=std::any_of(townPlayers.begin(),townPlayers.end(),[&](EntityId id){return !cainAttempt_.townPlayers.contains(id);});
                    if(cainAttempt_.marker!=source.id || requested!=cainAttempt_.requested || entered) cainAttempt_.attempted=false;
                    cainAttempt_.marker=source.id;cainAttempt_.requested=requested;cainAttempt_.townPlayers=std::move(townPlayers);
                    if(!requested || cainAttempt_.attempted) continue;
                }
                if(ports_.monsters.read().actors.size()>=65536) return StepStatus::Blocked;
                if(npc.rule.questInitFunction==54) {
                    const auto spawned=spawnTownCain(npc,region,source.position,tick.tick);
                    if(!spawned) return isDeferred(spawned.status) || spawned.status==DomainStatus::Stale
                        ?StepStatus::Blocked:StepStatus::NotImplemented;
                    cainAttempt_.attempted=true;
                    if(!*spawned.value) continue;
                } else if(!ports_.monsters.find(npc.id)) {
                    const auto admitted=ports_.quests.admitStoryNpc(npc,region,tick.tick);
                    if(!admitted) return isDeferred(admitted.status) || admitted.status==DomainStatus::Stale?StepStatus::Blocked:StepStatus::NotImplemented;
                    if(!*admitted.value) continue;
                }
                admittedPresetNpcs_.insert(npc.id);
            }
            return StepStatus::Complete;
        };
        if(admittedPresets_.contains(source.id)) {
            const auto children=admitChildren();if(children!=StepStatus::Complete) return children;
            continue;
        }
        if(source.questSpawn && (ports_.quests.read().actTwo.tomb!=region || !ports_.quests.read().actTwo.tombAt || tick.tick<ports_.quests.read().actTwo.tombAt)) continue;
        if(state_.objects.size()>=65536) return StepStatus::Blocked;
        Object object{source.id,source.type,region,source.position,1,source.initialMode,source.rule,0,0,false,2*source.rule.parameters[2]};
        object.resurrected=true;object.pd2Binding=source.pd2BirthClass>=0?-1:source.pd2Binding;object.pd2Portal=source.pd2Portal;
        auto random=ports_.random,controlRandom=state_.controlRandom.value();
        if(source.preset==580 || source.preset==581) {
            int selected=0;
            if(area.definition.act==1) {
                constexpr int tomb[]{87,88},arcane[]{387,389,390,391};
                selected=int(region)==74?arcane[limitedRandom(controlRandom,4)]:tomb[limitedRandom(controlRandom,2)];
            } else if(area.definition.act==2) {
                constexpr int jungle[]{181,183},temple[]{329,330,331,332};
                selected=int(region)==83?temple[limitedRandom(controlRandom,4)]:jungle[limitedRandom(controlRandom,2)];
            } else {
                constexpr int classes[]{5,6,139,140,141,144,176,177,198,240,241,242,243};
                selected=classes[limitedRandom(controlRandom,13)];
            }
            if(source.preset==580 && int(region)==25) selected=371;
            object.definition=selected;
            object.rule=area.definition.objectRules.at(selected);
            object.rule.specialChest=source.preset==580;
            if(selected==371) object.rule.towerReward=source.rule.towerReward;
            object.uses=2*object.rule.parameters[2];
        }
        if(source.questSpawn) {object.mode=ports_.quests.read().actTwo.tombOpen?2:1;object.until=tick.tick+std::max(uint64_t{1},object.rule.openingTicks);}
        if(source.rule.operation==23 && area.definition.town) object.mode=2;
        if(source.rule.operation==10 && int(region)==38 && ports_.quests.read().actOne.cainRescued) object.mode=5;
        if(source.rule.operation==9 && int(region)==4 && ports_.quests.read().actOne.restoreCairnStones) object.mode=2;
        initialize(object,tick.tick,random,controlRandom);
        if(object.pd2Portal) {object.mode=object.pd2Portal->sourceMode;object.until=0;}
        auto ids=ports_.ids;
        std::vector<std::pair<items::PreparedBatch,GroundLocation>> gold;
        std::vector<EntityId> goldIds;
        const auto initialized=prepareGoldPlaceholder(object,random,controlRandom,ids,gold,goldIds,ports_.spatial.nativeCollisionSample());
        if(!initialized) return isDeferred(initialized.status)?StepStatus::Blocked:StepStatus::NotImplemented;
        monsters::NativeBirthBatch births;
        std::optional<RoomRandomLoan> soulRandom;
        const auto souls=prepareSoulPlaceholder(object,tick.tick,random,controlRandom,ids,births,soulRandom,ports_.spatial.nativeCollisionSample());
        if(!souls) return isDeferred(souls.status)?StepStatus::Blocked:StepStatus::NotImplemented;
        EntityId paired;
        if(source.pd2BirthClass>=0) {
            const auto born=preparePairedPreset(object,source.pd2BirthClass,tick.tick,random,ids,births,soulRandom);
            if(!born) return isDeferred(born.status)?StepStatus::Blocked:StepStatus::NotImplemented;
            paired=*born.value;
        }
        auto actors=ports_.monsters.prepareActorPublication(std::move(births.actors),size_t(ids.cursor()-ports_.ids.cursor()));
        if(!actors.identities) actors.identities=ports_.ids.preparePublication(size_t(ids.cursor()-ports_.ids.cursor()));
        actors.identities->bind(source.id,NativeUnitType::Object);
        auto room=ports_.areas.prepareRoomUnit(source.id,2,region,object.position);
        const auto installed=ports_.items.installNativeGroundBatch(gold,tick.tick,goldIds,actors.identities);
        if(!installed) return isDeferred(installed.status)?StepStatus::Blocked:StepStatus::NotImplemented;
        if(object.rule.initialization==79) {
            const auto zoo=ports_.quests.initializeZoo(region,tick.tick);
            if(!zoo) return isDeferred(zoo.status)?StepStatus::Blocked:StepStatus::NotImplemented;
        }
        finishInitialization(object,controlRandom);
        state_.objects.emplace(source.id,std::move(object));
        publishInitialization(state_.objects.at(source.id));
        admittedPresets_.insert(source.id);presetObjects_.emplace(source.id,source.id);
        if(state_.objects.at(source.id).rule.initialization==47 && towerChests_.size()<8)
            towerChests_.push_back(source.id);
        ports_.monsters.publishBirthComponents(births);ports_.monsters.publishActors(actors);
        ports_.areas.publishRoomUnit(*room);
        if(paired) {
            const auto *unit=ports_.monsters.find(paired);
            ports_.maps.bind(0,paired,unit->area,unit->position,false);
            auto &object=state_.objects.at(source.id);object.pd2Binding=source.pd2Binding;
            ports_.maps.bind(object.pd2Binding,object.id,object.area,object.position,true);
        }
        if(soulRandom) ports_.areas.publishRoomRandom(*soulRandom);
        ports_.random=random;state_.controlRandom=controlRandom;changed.insert(region);
        collision(region);
        const auto children=admitChildren();if(children!=StepStatus::Complete) return children;
    }
    for(const auto region:changed) collision(region);
    return StepStatus::Complete;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    for(auto it=approaches_.begin();it!=approaches_.end();) {
        auto pending=it->second;pending.actor.tick=tick.tick;
        const auto *player=ports_.players.find(it->first);const auto *area=ports_.areas.find(pending.actor.area);
        const auto object=state_.objects.find(pending.request.target.id);
        if(!player || !player->entered || player->actor!=pending.actor.actor || player->area!=pending.actor.area ||
            player->persistent.player.hp<=0 || !area || area->generation!=pending.actor.areaGeneration ||
            player->locomotionSequence!=pending.locomotion || object==state_.objects.end()) {
            it=approaches_.erase(it);continue;
        }
        const auto &at=object->second;const auto &rule=at.rule;
        const auto point=ports_.areas.linkedPosition(pending.actor.area,at.area,at.position);
        if(!point || (at.area!=pending.actor.area && (!rule.door || rule.operation!=8))) {it=approaches_.erase(it);continue;}
        if(!interactionClear(area->definition.collision,player->position,{at.id,*point,*point,rule.width,rule.height,float(rule.range),true})) {
            if(player->route.empty()) it=approaches_.erase(it);else ++it;
            continue;
        }
        const auto result=execute(pending.actor,pending.request);
        if(isDeferred(result.status)) ++it;else it=approaches_.erase(it);
        if(ports_.monsters.pendingModifiers()) return StepStatus::Blocked;
    }
    std::set<RegionId> changed;
    for (auto &[id, object] : state_.objects) {
        if (!ports_.areas.loaded(object.area, object.position)) continue;
        const auto advanceUrn=[&] {
            if(!object.urnPending) return StepStatus::Complete;
            collision(object.area);
            const auto result=ports_.population.requestObjectBirth(object.area,id,object.position,tick);
            if(!result) return isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale?
                StepStatus::Blocked:StepStatus::NotImplemented;
            object.urnPending=false;return StepStatus::Complete;
        };
        if(const auto status=advanceUrn();status!=StepStatus::Complete) return status;
        if(const auto mode=ports_.quests.objectMode(object.area,id,object.definition,object.rule.operation,tick.tick);mode && object.mode!=*mode) {
            object.mode=*mode;object.until=object.rule.operation==49?ports_.quests.forgeAnimationEnd(id):ports_.quests.ancientObjectAnimationEnd(id);++object.revision;changed.insert(object.area);
        }
        if(object.rule.operation==25) {
            const auto &quest=ports_.quests.read().actTwo;
            const bool leased=std::any_of(quest.orifices.begin(),quest.orifices.end(),[&](const auto &entry){return entry.second==id;});
            const int mode=quest.tombAt && quest.tomb==object.area?2:leased?1:0;
            if(object.mode!=mode) {object.mode=mode;object.until=0;++object.revision;changed.insert(object.area);}
        }
        if(object.definition==318 && int(object.area)==40) {
            // A2Q4 Harem blocker is a shared game object, not a client guess.
            const bool opened=ports_.quests.read().npcStory.palaceOpen;
            if(opened && object.mode!=2) {object.mode=2;++object.revision;changed.insert(object.area);}
        }
        if(object.definition==153 && int(object.area)==73 && ports_.quests.read().actTwo.durielSlain && object.mode!=2) {object.mode=2;++object.revision;changed.insert(object.area);}
        if(object.rule.operation==9 && int(object.area)==4 && ports_.quests.read().actOne.restoreCairnStones && object.mode!=2) {
            object.mode=2;object.until=0;++object.revision;changed.insert(object.area);
        }
        if(object.pending) if(const auto result=ports_.quests.takeCompletion(id)) {
            object.pending=false;
            if(*result) {object.mode=ports_.quests.objectMode(object.area,id,object.definition,object.rule.operation,tick.tick).value_or(1);object.until=object.mode==1?(object.rule.operation==49?ports_.quests.forgeAnimationEnd(id):tick.tick+std::max(uint64_t{1},object.rule.openingTicks)):0;++object.revision;changed.insert(object.area);}
        }
        if(object.pending && !towerOpening_.contains(id) && !ports_.events.hasCapacity(1)) {
            for(const auto area:changed) collision(area);
            return StepStatus::Blocked;
        }
        if (object.pending && !towerOpening_.contains(id)) if (const auto result = ports_.loot.takeCompletion(id)) {
            object.pending = false;
            if (*result) {
                if(object.rule.shrine) object.specialPending=true;
                else if(object.rule.operation!=51) {
                    // ObjMode::OBJECTS_ChestEnd skips OP when Objects.Mode1 is
                    // absent. Retail clients halt on that unsupported mode.
                    if(object.rule.operation==4) object.mode=object.rule.openingMode?1:2;
                    else object.mode=object.rule.operation==26 || object.rule.operation==19 || object.rule.operation==20?2:object.rule.operation==48?(object.phase?3:5):1;
                    object.until=object.mode==1?tick.tick+std::max(uint64_t{1},object.rule.openingTicks):0;
                    if(object.mode==3) object.hazardAt=tick.tick+object.rule.closingTicks;
                    object.trap=object.rule.trap;
                    if(object.rule.chest) {object.rule.chest->locked=false;object.trap=object.definition==623 && ports_.areas.at(object.area).definition.pd2Map?0:object.rule.chest->trap;}
                    if(object.rule.operation==1 && limitedRandom(object.random,10000)>=8192) object.trap=8;
                    if(object.rule.operation==68 && limitedRandom(*state_.controlRandom,255)<=unsigned(object.rule.parameters[7])) object.urnPending=true;
                    if((object.trap==8 || object.trap==9) && ports_.areas.at(object.area).definition.act==0) {
                        if(ports_.areas.at(object.area).definition.populationRules.chestBaseClass==234) object.trap=0;
                    }
                    const auto scheduled=scheduleTrap(object,tick.tick);
                    if(!scheduled) throw std::logic_error("Preflighted native trap notification rejected");
                    ++object.revision;changed.insert(object.area);
                    if(const auto status=advanceUrn();status!=StepStatus::Complete) return status;
                }
            }
        }
        if(object.specialPending) {
            const auto *player=ports_.players.find(object.operatorId);const auto &area=ports_.areas.at(object.area);
            DomainResult<> result{DomainStatus::Applied,std::monostate{}};
            if(object.rule.shrine->code!=18 && player) result=shrineMissiles({player->player,player->actor,object.area,area.generation,0,tick.tick},object);
            if(result) {object.specialPending=false;object.mode=1;object.until=!object.rule.cycleOperating && object.rule.openedMode?tick.tick+std::max(uint64_t{1},object.rule.openingTicks):0;object.reset=object.rule.shrine->reset>0?tick.tick+uint64_t(object.rule.shrine->reset)+1:0;++object.revision;changed.insert(object.area);}
        }

    }
    for (const auto area : changed) collision(area);
    return StepStatus::Complete;
}

}
