#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/units/distance.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>

// D2MOO A2Q4 palace callbacks, A3Q3/A3Q4 NPC deactivation, A1Q4/A5Q3
// portal choreography and QUESTS_MapPresetSpawn. MIT: docs/licenses/D2MOO.txt.
namespace d2x::server::quests {
namespace {
int coordinateDistance(Vec from,Vec to) {
    return unitCoordinateDistance(int(std::floor(from.x)),int(std::floor(from.y)),int(std::floor(to.x)),int(std::floor(to.y)));
}
}
bool System::npcInteracting(EntityId id) const {
    for(const auto &[player,conversation]:ports_.npc.read().conversations)
        if(conversation.npc==id && ports_.npc.conversation(player)) return true;
    return false;
}
void System::registerNpcStoryObject(EntityId id,RegionId region,int definition,int initialization,Vec position) {
    auto &story=state_.npcStory;
    if(!story.markers.insert(id).second) return;
    const auto &area=ports_.areas.at(region).definition;
    if(int(region)==40 && initialization==19) story.guard=position+area.origin+Vec{1,0};
    if(int(region)==40 && initialization==30) {story.blockerId=id;story.blocker=position+area.origin;}
    if(int(region)==75 && initialization==39) story.ormusAltar=id;
    if(int(region)==114 && definition==460) story.anyaOutsidePosition=position+area.origin;
    for(const auto &input:area.npcMapAi) if(input.objectClass==definition && input.position.x==position.x && input.position.y==position.y) {
        auto &buffer=story.mapAi[input.npc];
        if(!buffer.consumed) buffer.nodes=input.nodes;
        // The original late-marker callback resolves the town actor only.
        // An already born outside Anya must not consume this later buffer.
        std::vector<EntityId> targets;
        for(const auto &[actor,unit]:ports_.monsters.read().actors)
            if(unit.area==region && unit.questNpc && unit.alive() && unit.identity.monster==input.npc) targets.push_back(actor);
        for(const auto actor:targets) bindNpcMapAi(actor);
    }
}
DomainResult<> System::bindNpcMapAi(EntityId id) {
    const auto *unit=ports_.monsters.find(id);
    if(!unit || !unit->questNpc || !unit->alive()) return {DomainStatus::InvalidActor,{}};
    const auto code=unit->identity.monster=="drehyaiced"?std::string_view("drehya"):std::string_view(unit->identity.monster);
    const auto found=state_.npcStory.mapAi.find(code);
    if(found==state_.npcStory.mapAi.end() || found->second.consumed || found->second.nodes.empty())
        return {DomainStatus::Applied,std::monostate{}};
    const auto result=ports_.monsters.rebindNpcMapAi(id,found->second.nodes);
    if(result) {found->second.consumed=true;found->second.nodes.clear();}
    return result;
}
DomainResult<EntityId> System::spawnStoryNpc(RegionId region,std::string_view code,Vec position,uint64_t tick,bool critical) {
    const auto &area=ports_.areas.at(region).definition;
    for(const auto &npc:area.npcs) if(npc.rule.code==code)
        return ports_.monsters.find(npc.id)?DomainResult<EntityId>{DomainStatus::Applied,npc.id}:DomainResult<EntityId>{DomainStatus::Stale,{}};
    const auto rule=area.questNpcs.find(code);
    if(rule==area.questNpcs.end() || !rule->second.combat) return {DomainStatus::NotImplemented,{}};
    const auto *bounds=area.activation.room(position);if(!bounds) return {DomainStatus::Unavailable,{}};
    const RoomView initial{region,(int(area.origin.x)+bounds->x)/5,(int(area.origin.y)+bounds->y)/5};
    const auto original=position+area.origin;
    auto room=initial;auto point=position;
    const auto sample=ports_.spatial.nativeCollisionSample();
    const auto freePosition=[&](int radius) {
        const auto global=point+area.origin;
        const auto free=ports_.areas.nativeQuestFreePosition(room,int(global.x),int(global.y),2,0x100,radius,sample);
        if(free) {room=free->room;point=free->position;}
        else {room=initial;point=position;}
        return free.has_value();
    };
    if(critical) {
        int x=int(original.x),y=int(original.y),counter=0;
        while(x<int(area.origin.x)+bounds->x || y<int(area.origin.y)+bounds->y ||
            x>=int(area.origin.x)+bounds->x+bounds->width-1 || y>=int(area.origin.y)+bounds->y+bounds->height-1) {
            ++x;++y;
            if(++counter>20) {x=int(original.y);break;}
        }
        point=Vec{float(x),float(y)}-area.origin;freePosition(6);
    }
    std::map<RoomView,RoomRandomLoan> loans;
    const auto attempt=[&](int count) -> DomainResult<std::optional<Vec>> {
        if(room.area!=region) return {DomainStatus::NotImplemented,{}};
        auto loan=loans.find(room);
        if(loan==loans.end()) {
            const auto borrowed=ports_.areas.roomRandom(room);if(!borrowed) return {borrowed.status,{}};
            loan=loans.emplace(room,*borrowed.value).first;
        }
        monsters::Admission request;request.identity.monster=code;request.identity.spawnKey="quest.npc";
        request.area=region;request.position=point;request.rule=*rule->second.combat;request.tick=tick;
        return ports_.monsters.searchRoomSpawn(request,loan->second,count,sample);
    };
    auto placed=attempt(-1);if(!placed) return {placed.status,{}};
    if(critical) {
        if(!*placed.value) {placed=attempt(5);if(!placed) return {placed.status,{}};}
        for(int retry=0;retry<20 && !*placed.value;++retry) {
            auto global=point+area.origin+Vec{1,1};
            const auto next=ports_.areas.roomAtPosition(region,point,int(global.x),int(global.y));
            if(next) {room=*next;point=global-area.origin;}
            else {room=initial;point=position;}
            freePosition(7);placed=attempt(10);if(!placed) return {placed.status,{}};
        }
        if(!*placed.value) {room=initial;point=position;placed=attempt(15);if(!placed) return {placed.status,{}};}
    } else if(!*placed.value) {
        // ACT1Q4_SpawnCainInTristram: exactly one size2/radius3 fallback.
        if(freePosition(3)) {placed=attempt(-1);if(!placed) return {placed.status,{}};}
    }
    for(const auto &[view,loan]:loans) {(void)view;if(!ports_.areas.validateRoomRandom(loan)) return {DomainStatus::Stale,{}};}
    if(!*placed.value) {
        for(const auto &[view,loan]:loans) {(void)view;ports_.areas.publishRoomRandom(loan);}
        return {DomainStatus::Unavailable,{}};
    }
    const auto admitted=ports_.world.admitQuestNpc(region,code,**placed.value,0);
    if(!admitted) return admitted;
    for(const auto &[view,loan]:loans) {(void)view;ports_.areas.publishRoomRandom(loan);}
    return admitted;
}
uint64_t System::cainBirthDue(EntityId id) const {
    const auto &story=state_.npcStory;
    return id==story.cainCage && !story.cainOutsideSpawned?story.cainBirthAt:0;
}
DomainResult<> System::cainBirthEvent(EntityId id,uint64_t tick) {
    auto &story=state_.npcStory;
    if(!cainBirthDue(id) || !story.cainBirth) return {DomainStatus::Applied,std::monostate{}};
    const auto &area=ports_.areas.at(RegionId(38)).definition;
    const auto spawned=story.cainBirthFailed?DomainResult<EntityId>{DomainStatus::Unavailable,{}}:
        spawnStoryNpc(RegionId(38),"cain1",*story.cainBirth-area.origin,tick,false);
    if(!spawned && spawned.status!=DomainStatus::Unavailable) return {spawned.status,{}};
    if(!spawned) {
        story.cainBirthFailed=true;
        // Native failure still releases the town Cain and supplies a blue
        // return portal; do not leave the completed rescue waiting forever.
        for(const auto &[playerId,player]:ports_.players.all()) {
            (void)playerId;if(!player.entered || int(player.area)!=38) continue;
            const auto portal=ports_.travel.prepareSpecialPortal({player.player,player.actor,player.area,ports_.areas.at(player.area).generation,0,tick},
                travel::SpecialPortalKind::CainRescue,0,*story.cainBirth-area.origin+Vec{3,3});
            if(!portal) return {portal.status,{}};
            ports_.travel.commitSpecialPortal(*portal.value);break;
        }
        story.cainDeparted=true;
    }
    captureGoal(QuestId::SearchForCain,uint32_t(CainStage::Rescued),RegionId(38));
    story.cainOutsideSpawned=true;story.cainBirthAt=0;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<std::optional<Vec>> System::npcSpawnPosition(const AreaNpc &npc,RegionId region,uint64_t) {
    const auto allowed=[](std::optional<Vec> point) {return DomainResult<std::optional<Vec>>{DomainStatus::Applied,point};};
    auto &story=state_.npcStory;
    if(story.retired.contains(npc.id)) return allowed({});
    const auto &code=npc.rule.code;const int initialization=npc.rule.questInitFunction;
    const auto &area=ports_.areas.at(region).definition;
    const auto freePosition=[&](Vec point,int size,int radius) -> DomainResult<std::optional<Vec>> {
        const auto global=point+area.origin;
        const auto room=ports_.areas.roomAtPosition(region,npc.sourceObjectPosition.value_or(npc.spawnPosition),int(global.x),int(global.y));
        if(!room) return allowed({});
        const auto free=ports_.areas.nativeQuestFreePosition(*room,int(global.x),int(global.y),size,0x100,radius,ports_.spatial.nativeCollisionSample());
        if(!free || free->room.area!=region) return allowed({});
        return allowed(free->position);
    };
    if(int(region)==40 && code=="jerhyn" && (initialization==18 || initialization==19)) {
        if((initialization==18)==story.palaceRequested) return allowed({});
        if(initialization==19) {
            for(const auto &candidate:area.npcs) if(candidate.rule.code=="jerhyn" && candidate.rule.questInitFunction==18 && ports_.monsters.find(candidate.id))
                return allowed({});
            bool late=state_.actTwo.durielSlain || (state_.initialFlags[questDefinition(QuestId::SevenTombs).nativeSlot]&0x8000);
            for(const auto &[id,player]:ports_.players.all()) {
                (void)id;
                if(player.entered && player.persistent.player.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::SevenTombs)].stage>=uint32_t(TombsStage::Assigned)) late=true;
            }
            // ACT2Q4_InitializeJerhynMonster, relative to the actual marker.
            const auto marker=npc.sourceObjectPosition.value_or(npc.spawnPosition);
            return freePosition(marker+Vec{late?15.f:-10.f,-3},3,9);
        }
        return freePosition(npc.spawnPosition,2,10);
    }
    if(int(region)==40 && code=="act2guard2" &&
       ((state_.initialFlags[questDefinition(QuestId::SevenTombs).nativeSlot]&0x8000) || state_.actTwo.tyraelPortal)) return allowed({});
    if(int(region)==75 && code=="hratli" && (initialization==49 || initialization==50)) {
        if((initialization==49)==story.hratliDeparted) return allowed({});
        if(initialization==50) for(const auto &candidate:area.npcs)
            if(candidate.rule.code==code && candidate.rule.questInitFunction==49 && ports_.monsters.find(candidate.id)) return allowed({});
    }
    if(int(region)==75 && code=="natalya" && state_.actThree.mephistoSlain) return allowed({});
    if(int(region)==109 && code=="nihlathak" && state_.actFive.anyaThawed) return allowed({});
    if(int(region)==109 && code=="larzuk" && initialization==71) return freePosition(npc.spawnPosition,2,16);
    return allowed(npc.spawnPosition);
}
DomainResult<bool> System::admitStoryNpc(const AreaNpc &npc,RegionId region,uint64_t tick) {
    if(ports_.monsters.find(npc.id)) return {DomainStatus::Applied,true};
    for(const auto &[room,units]:ports_.monsters.read().inactive) {
        (void)room;
        if(std::any_of(units.begin(),units.end(),[&](const auto &unit){return unit.id==npc.id;})) return {DomainStatus::Applied,false};
    }
    const auto point=npcSpawnPosition(npc,region,tick);if(!point) return {point.status,{}};
    if(!*point.value) return {DomainStatus::Applied,false};
    if(!npc.rule.combat) return {DomainStatus::NotImplemented,{}};
    const auto &area=ports_.areas.at(region).definition;
    const auto global=**point.value+area.origin;
    const auto room=ports_.areas.roomAtPosition(region,**point.value,int(global.x),int(global.y));
    if(!room) return {DomainStatus::Applied,false};
    auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
    monsters::Admission request;request.identity.monster=npc.rule.code;request.identity.spawnKey="npc."+std::to_string(npc.id.value);
    request.area=region;request.position=**point.value;request.rule=*npc.rule.combat;request.tick=tick;
    const auto sample=ports_.spatial.nativeCollisionSample();
    auto placed=ports_.monsters.searchRoomSpawn(request,*loan.value,-1,sample);
    if(!placed) return {placed.status,{}};
    const int fallback=npc.rule.code=="jerhyn" && npc.rule.questInitFunction==19?2:
        npc.rule.code=="natalya"?3:npc.rule.code=="larzuk"?5:-1;
    if(!*placed.value && fallback>=0) {
        placed=ports_.monsters.searchRoomSpawn(request,*loan.value,fallback,sample);
        if(!placed) return {placed.status,{}};
    }
    if(!ports_.areas.validateRoomRandom(*loan.value)) return {DomainStatus::Stale,{}};
    if(!*placed.value) {ports_.areas.publishRoomRandom(*loan.value);return {DomainStatus::Applied,false};}
    const auto admitted=ports_.monsters.admitNpc(npc.id,region,**placed.value,npc.rule);
    if(!admitted) return {admitted.status,{}};
    ports_.areas.publishRoomRandom(*loan.value);
    const auto bound=bindNpcMapAi(npc.id);if(!bound) return {bound.status,{}};
    return {DomainStatus::Applied,true};
}
DomainResult<NpcAiResult> System::npcThink(EntityId id,uint64_t tick) {
    auto *unit=ports_.monsters.find(id);
    if(!unit || !unit->questNpc || !unit->alive()) return {DomainStatus::Applied,NpcAiResult{}};
    const auto &area=ports_.areas.at(unit->area).definition;
    const auto global=unit->position+area.origin;
    auto &story=state_.npcStory;
    const auto done=[](NpcAiResult result={}) {return DomainResult<NpcAiResult>{DomainStatus::Applied,result};};
    const auto idle=[&](int frames) -> DomainResult<NpcAiResult> {
        const auto result=ports_.monsters.aiPose(id,1,false,false,tick);
        if(!result) return {result.status,{}};
        return done({true,false,false,frames,{}});
    };
    const auto walk=[&](Vec point) -> DomainResult<NpcAiResult> {
        if(!unit->route.empty()) return done({true,true,false,1,{}});
        const auto result=ports_.monsters.requestMove({id,{unit->area,ports_.areas.at(unit->area).generation,point-area.origin},{},0,75,false,1,0,0,7,5});
        if(!result) {
            if(isDeferred(result.status)) return {result.status,{}};
            return idle(unit->rule.neutralDelayTicks);
        }
        return done({true,true,false,1,{}});
    };
    const auto retire=[&]() -> DomainResult<NpcAiResult> {
        const auto result=ports_.monsters.retireQuestNpc(id);
        if(!result) return {result.status,{}};
        story.retired.insert(id);return done({true,false,true,1,{}});
    };
    const auto pose=[&](uint8_t mode) -> DomainResult<NpcAiResult> {
        const auto clock=unit->rule.aiPoseTicks.find(mode);
        if(clock==unit->rule.aiPoseTicks.end()) return {DomainStatus::NotImplemented,{}};
        const auto result=ports_.monsters.aiPose(id,mode,false,false,tick);
        if(!result) return {result.status,{}};
        return done({true,false,false,clock->second+unit->rule.neutralDelayTicks,{}});
    };
    const auto &code=unit->identity.monster;
    if(code=="jerhyn" && int(unit->area)==40) {
        const auto source=std::find_if(area.npcs.begin(),area.npcs.end(),[&](const auto &npc){return npc.id==id;});
        if(source==area.npcs.end() || source->rule.questInitFunction!=19) return done();
        if(!unit->route.empty()) return done({true,true,false,1,{}});
        bool radamentFinished=(state_.initialFlags[questDefinition(QuestId::RadamentsLair).nativeSlot]&0x8000)!=0;
        bool tombsFinished=(state_.initialFlags[questDefinition(QuestId::SevenTombs).nativeSlot]&0x8000)!=0;
        bool arcaneStarted=story.palaceOpen;
        bool tombsBriefed=false;
        for(const auto &[playerId,player]:ports_.players.all()) {
            (void)playerId;if(!player.entered) continue;
            const auto &book=player.persistent.player.quests.at(size_t(ports_.settings.difficulty));
            radamentFinished|=book[questIndex(QuestId::RadamentsLair)].stage>=uint32_t(RadamentStage::Slain);
            tombsFinished|=book[questIndex(QuestId::SevenTombs)].stage>=uint32_t(TombsStage::JerhynConfirmed);
            tombsBriefed|=book[questIndex(QuestId::SevenTombs)].stage>=uint32_t(TombsStage::Assigned);
            arcaneStarted|=book[questIndex(QuestId::ArcaneSanctuary)].stage>0;
        }
        if(!radamentFinished && !tombsFinished && !arcaneStarted) return idle(40);
        if(story.palaceRelocated && !story.palaceHomeShifted) {
            const auto home=global+Vec{9,0};
            const auto changed=ports_.monsters.setNpcHomeCommand(id,home);if(!changed) return {changed.status,{}};
            story.palaceHomeShifted=true;
            auto result=idle(50);if(result) result.value->home=home;return result;
        }
        if(tombsFinished || tombsBriefed || arcaneStarted || state_.actTwo.durielSlain || story.palaceRelocated) return done();
        // A2Q1's sequence activates A2Q6 state1 before Jerhyn's 430 text;
        // personal TombsStage::Assigned is the later native state2.
        story.palaceApproach|=radamentFinished;
        if(!story.palaceApproach && !story.palaceApproached) return done();
        if(!story.blocker) return idle(20);
        if(!story.palaceApproached) {
            bool close=false;
            for(const auto &[playerId,player]:ports_.players.all()) {
                (void)playerId;if(!player.entered || ports_.areas.at(player.area).definition.act!=1) continue;
                const auto stage=player.persistent.player.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::SevenTombs)].stage;
                if(stage>=uint32_t(TombsStage::TyraelRescued)) continue;
                if(coordinateDistance(player.position+ports_.areas.at(player.area).definition.origin,*story.blocker)<=30) close=true;
            }
            if(!close) return idle(20);
            auto result=walk(*story.blocker);
            if(result && result.value->walking) {story.palaceApproached=true;story.palaceApproach=false;}
            return result;
        }
        if(coordinateDistance(global,*story.blocker)>2) {
            story.guardStepBack=true;story.guardAtEntrance=false;return walk(*story.blocker);
        }
        const auto object=ports_.objects.read().objects.find(story.blockerId);
        if(object==ports_.objects.read().objects.end()) return idle(20);
        const auto point=*story.blocker+Vec{4,0};
        const auto room=ports_.areas.roomAtPosition(unit->area,unit->position,int(point.x),int(point.y));
        if(!room) return idle(20);
        const auto sample=ports_.spatial.nativeCollisionSample();
        for(const int size:{1,2,3}) {
            const auto free=ports_.areas.nativeFreeCoordinates(*room,int(point.x),int(point.y),size,0x3c01,sample);
            if(!free || free->room.area!=unit->area) continue;
            const auto moved=ports_.monsters.moveNpc(id,unit->area,free->position,free->position,false);
            if(!moved) return {moved.status,{}};
            story.palaceRelocated=true;return idle(20);
        }
        return idle(20);
    }
    if(unit->rule.ai.kind==MonsterAiKind::JarJar) {
        if(!unit->aiHomeCommand) return idle(20);
        if(!unit->route.empty()) return done({true,true,false,1,{}});
        const bool guardFinished=story.palaceOpen && !story.guardAtEntrance && !story.guardStepBack;
        if(!guardFinished) {
            const auto previous=story.guardDecisionAt.find(id);
            const auto at=previous==story.guardDecisionAt.end()?uint64_t{0}:previous->second;
            if(at<tick && tick-at<200) return idle(20);
            const auto point=story.guardStepBack && story.guard?*story.guard+Vec{0,-4}:
                story.guardAtEntrance && story.guard?*story.guard:*unit->aiHomeCommand;
            if(coordinateDistance(global,point)>1) {
                auto result=walk(point);if(result && result.value->walking) story.guardDecisionAt[id]=tick;return result;
            }
            if(story.guardAtEntrance || story.guardStepBack) return idle(20);
            return done();
        }
        const auto point=*unit->aiHomeCommand+Vec{0,-3};
        if(coordinateDistance(global,point)>2) return walk(point);
        return idle(120);
    }
    if(code=="ormus" && story.ormusActivate) {
        const auto altar=ports_.objects.read().objects.find(story.ormusAltar);
        if(altar==ports_.objects.read().objects.end()) return idle(20);
        const auto point=altar->second.position+ports_.areas.at(altar->second.area).definition.origin;
        if(coordinateDistance(global,point)>3) return walk(point);
        const auto animated=pose(8);if(!animated) return animated;
        const auto opened=ports_.objects.setNativeMode(story.ormusAltar,1,tick,true);
        if(!opened) return {opened.status,{}};
        story.ormusActivate=false;story.ormusActivated=true;return animated;
    }
    if(code=="alkor" && story.alkorAnimate) {
        const auto result=pose(8);if(result) story.alkorAnimate=false;return result;
    }
    if(unit->rule.ai.kind==MonsterAiKind::NpcOutOfTown) {
        const bool anya=code=="drehyaiced";
        if((anya && !state_.actFive.anyaThawed) || (!anya && !state_.actOne.cainRescued)) return idle(40);
        auto [entry,fresh]=story.escapes.try_emplace(id);
        auto &escape=entry->second;
        if(fresh) {escape.start=global+Vec{3,3};escape.portal=global;escape.phase=1;return idle(1);}
        if(npcInteracting(id)) return idle(40);
        if(!unit->route.empty()) return done({true,true,false,1,{}});
        if(escape.phase<2) {
            if(coordinateDistance(global,escape.start)>1 && escape.attempts<=5) {
                auto result=walk(escape.start);if(result) ++escape.attempts;return result;
            }
            bool exists=false;
            for(const auto &[portalId,portal]:ports_.travel.read().specialPortals) {
                (void)portalId;if(portal.field==unit->area && int(portal.town)==(anya?109:1)) {escape.portal=portal.fieldPosition+area.origin;exists=true;break;}
            }
            if(!exists) {
                const PlayerState *player=nullptr;
                for(const auto &[playerId,candidate]:ports_.players.all()) {
                    (void)playerId;if(candidate.entered && candidate.area==unit->area && candidate.persistent.player.hp>0) {player=&candidate;break;}
                }
                if(!player) return idle(20);
                const auto portal=ports_.travel.prepareSpecialPortal({player->player,player->actor,unit->area,ports_.areas.at(unit->area).generation,0,tick},
                    anya?travel::SpecialPortalKind::AnyaRescue:travel::SpecialPortalKind::CainRescue,0,escape.portal-area.origin);
                if(!portal) {if(isDeferred(portal.status)) return {portal.status,{}};return idle(20);}
                for(const auto &[portalId,prepared]:portal.value->next) {
                    (void)portalId;if(prepared.field==unit->area && int(prepared.town)==(anya?109:1)) {escape.portal=prepared.fieldPosition+area.origin;break;}
                }
                ports_.travel.commitSpecialPortal(*portal.value);
            }
            escape.phase=2;return idle(20);
        }
        if(escape.phase+1<8 && coordinateDistance(global,escape.portal)>0) {
            auto result=walk(escape.portal);if(result) ++escape.phase;return result;
        }
        const auto result=retire();
        if(result) {if(anya) story.anyaDeparted=true;else story.cainDeparted=true;}
        return result;
    }
    if(code=="cain5" && int(unit->area)==1 && state_.actOne.cainRescued && !story.cainTownActivated) {
        for(const auto &[portalId,portal]:ports_.travel.read().specialPortals) {
            (void)portalId;if(int(portal.field)!=38 || int(portal.town)!=1) continue;
            const auto point=portal.townPosition+area.origin;
            if(coordinateDistance(global,point)>2) return walk(point);
            story.cainTownActivated=true;break;
        }
    }
    if(code=="drehya" && int(unit->area)==109 && story.anyaPortalPending && !state_.actFive.anyaPortal) {
        const auto portal=ports_.travel.prepareNativePermanentPortal(unit->area,unit->position+Vec{10,5},121,tick);
        if(!portal) return {portal.status,{}};
        ports_.travel.commitSpecialPortal(*portal.value);state_.actFive.anyaPortal=true;story.anyaPortalPending=false;
    }
    return done();
}
void System::npcInteractionDecision(EntityId id,uint64_t tick) {
    const auto *unit=ports_.monsters.find(id);
    if(unit && unit->rule.ai.kind==MonsterAiKind::JarJar) state_.npcStory.guardDecisionAt[id]=tick;
}
StepStatus System::npcStoryStep(TickContext tick) {
    bool blocked=false;auto &story=state_.npcStory;
    const auto difficulty=size_t(ports_.settings.difficulty);
    for(const auto &[id,player]:ports_.players.all()) {
        if(!player.entered) continue;
        const auto &record=player.persistent.player;const auto &book=record.quests.at(difficulty);
        const auto previous=story.playerAreas.find(id);
        if(previous==story.playerAreas.end()) {
            story.palaceRequested|=record.questPreludes.at(difficulty).at(size_t(QuestPreludeId::LutGholeinArrival)) ||
                book[questIndex(QuestId::RadamentsLair)].stage>=uint32_t(RadamentStage::Slain) || book[questIndex(QuestId::ArcaneSanctuary)].stage>0 || book[questIndex(QuestId::SevenTombs)].stage>0;
            story.palaceOpen|=book[questIndex(QuestId::TaintedSun)].stage>=uint32_t(SunStage::AltarDestroyed) ||
                book[questIndex(QuestId::HoradricStaff)].stage>=uint32_t(StaffStage::Submitted);
            if(!story.anyaThawAt && book[questIndex(QuestId::PrisonOfIce)].stage>=5) story.anyaDeparted=true;
        } else if(int(previous->second)==40 && int(player.area)!=40) story.palaceRequested=true;
        story.playerAreas.insert_or_assign(id,player.area);
        story.palaceOpen|=book[questIndex(QuestId::ArcaneSanctuary)].stage>0;
        story.hratliDeparted|=record.questPreludes.at(difficulty).at(size_t(QuestPreludeId::KurastArrival));
    }
    std::erase_if(story.playerAreas,[&](const auto &entry){const auto *player=ports_.players.find(entry.first);return !player || !player->entered;});
    // A5Q3's Malah 20132 callback also removes an inactive outside Anya;
    // returning to collect the scroll must not depend on that room waking up.
    if(story.anyaReturnRequested && !story.anyaDeparted) {
        bool removed=true;
        if(const auto *outside=ports_.areas.find(RegionId(114))) for(const auto &npc:outside->definition.npcs)
            if(npc.rule.code=="drehyaiced") {
                const auto result=ports_.monsters.retireQuestNpc(npc.id);
                if(result) story.retired.insert(npc.id);else {removed=false;blocked|=isDeferred(result.status);}
            }
        if(removed) {story.anyaDeparted=true;story.anyaIceOpened=true;story.anyaOutsideSpawned=true;story.anyaThawAt=0;}
    }
    if(story.palaceOpen && story.blockerId) {
        const auto object=ports_.objects.read().objects.find(story.blockerId);
        if(object!=ports_.objects.read().objects.end() && object->second.mode!=2) {
            const auto opened=ports_.objects.setNativeMode(story.blockerId,2,tick.tick);if(!opened) blocked=true;
        }
    }
    // Arrival markers create one real NPC at a time. Revisit already loaded
    // markers after quest changes; a room's population cursor need not restart.
    for(const auto &[region,area]:ports_.areas.all()) {
        for(const auto &npc:area.definition.npcs) {
            if(!npc.sourceObjectPosition || story.retired.contains(npc.id)) continue;
            const bool supported=(int(region)==40 && (npc.rule.questInitFunction==18 || npc.rule.questInitFunction==19)) ||
                (int(region)==75 && (npc.rule.questInitFunction==49 || npc.rule.questInitFunction==50 || npc.rule.questInitFunction==52)) ||
                (int(region)==109 && (npc.rule.questInitFunction==68 || npc.rule.questInitFunction==71));
            if(!supported) continue;
            const auto *unit=ports_.monsters.find(npc.id);
            const bool remove=(npc.rule.code=="jerhyn" && npc.rule.questInitFunction==18 && story.palaceRequested) ||
                (npc.rule.code=="hratli" && npc.rule.questInitFunction==49 && story.hratliDeparted && (!unit || !ports_.areas.active(region,unit->position))) ||
                (npc.rule.code=="nihlathak" && state_.actFive.anyaThawed) || (npc.rule.code=="natalya" && state_.actThree.mephistoSlain);
            if(remove) {
                if(npcInteracting(npc.id)) continue;
                const auto result=ports_.monsters.retireQuestNpc(npc.id);
                if(result) story.retired.insert(npc.id);else blocked|=isDeferred(result.status);
                continue;
            }
            const auto marker=std::find_if(ports_.objects.read().objects.begin(),ports_.objects.read().objects.end(),[&](const auto &entry) {
                const auto &object=entry.second;return object.area==region && object.definition==npc.sourceObjectClass &&
                    object.position.x==npc.sourceObjectPosition->x && object.position.y==npc.sourceObjectPosition->y;
            });
            if(marker==ports_.objects.read().objects.end()) continue;
            if(unit || !ports_.areas.loaded(region,*npc.sourceObjectPosition)) continue;
            const auto admitted=admitStoryNpc(npc,region,tick.tick);
            if(!admitted) {blocked|=isDeferred(admitted.status);continue;}
        }
    }
    if(story.anyaThawAt && !story.anyaOutsideSpawned && tick.tick>=story.anyaThawAt && story.anyaOutsidePosition) {
        const auto *area=ports_.areas.find(RegionId(114));
        if(area && ports_.areas.loaded(RegionId(114),*story.anyaOutsidePosition-area->definition.origin)) {
            if(!story.anyaIceOpened) {
                const auto opened=ports_.objects.setNativeMode(story.anyaFrozenObject,2,tick.tick);
                if(opened) {story.anyaIceOpened=true;story.anyaThawAt=(tick.tick/20+2)*20;}else blocked|=isDeferred(opened.status);
            } else {
                const auto spawned=spawnStoryNpc(RegionId(114),"drehyaiced",*story.anyaOutsidePosition-area->definition.origin,tick.tick,true);
                if(spawned) {
                    const auto bound=bindNpcMapAi(*spawned.value);
                    if(bound) story.anyaOutsideSpawned=true;else blocked|=isDeferred(bound.status);
                } else blocked|=isDeferred(spawned.status);
            }
        }
    }
    if(story.anyaOutsideSpawned && ports_.objects.read().objects.contains(story.anyaFrozenObject)) {
        const auto removed=ports_.objects.removeNative(story.anyaFrozenObject);if(!removed) blocked|=isDeferred(removed.status);
    }
    std::vector<EntityId> townBindings;
    for(const auto &[id,unit]:ports_.monsters.read().actors) {
        if(!unit.questNpc || !unit.alive() || !ports_.areas.at(unit.area).definition.town) continue;
        const auto buffer=story.mapAi.find(unit.identity.monster);
        if(buffer!=story.mapAi.end() && !buffer->second.consumed) townBindings.push_back(id);
    }
    for(const auto id:townBindings) {const auto bound=bindNpcMapAi(id);if(!bound) blocked|=isDeferred(bound.status);}
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
