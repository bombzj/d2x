#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/world/system.hpp"
#include "gameplay/maps/pd2/event_program.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x::server::maps {
namespace {
std::string_view noticeKey(int program) {
    switch(program) {
    case 11:return "MapEventNotifyMendeln";
    case 12:return "MapEventNotifyHorazon";
    case 14:return "MapEventNotifyDarkWanderer";
    case 15:return "MapEventNotifyTreasure";
    case 16:return "MapEventNotifyTainted";
    case 18:return "MapEventNotifyGheed";
    case 19:return "MapEventNotifyShard";
    case 20:return "MapEventNotifyInvaders";
    case 21:return "MapEventNotifyTomb";
    default:return {};
    }
}
}
DomainResult<> System::notify(std::string_view key,uint8_t color,TickContext tick) {
    if(!state_.instance) return {DomainStatus::Unavailable,{}};
    const auto found=state_.instance->catalog->notices.find(key);
    if(found==state_.instance->catalog->notices.end() || found->second.size()>255) return {DomainStatus::NotImplemented,{}};
    std::vector<DomainFact> facts;
    for(const auto &[id,player]:ports_.players.all()) if(player.entered) {
        // 37DA60: packet26 / type4 / language0 / unit2 / GUID0,
        // message color argument, name color36, recipient's own name.
        ChatFact message{{},player.persistent.player.name,found->second,{id}};
        message.type=4;message.nameColor=0x36;message.messageColor=color;
        facts.emplace_back(std::move(message));
    }
    if(facts.empty()) return {DomainStatus::Applied,std::monostate{}};
    const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Game,{},{}},std::move(facts)});
    return sent?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{sent.status,{}};
}
DomainResult<bool> System::populateRoom(RegionId region,size_t index,int processed,TickContext tick) {
    synchronize();
    const auto scene=initializeSceneRoom(region,tick);if(!scene) return {scene.status,{}};
    const auto &area=ports_.areas.at(region);
    if(!state_.instance || area.definition.pd2Map!=state_.instance || !pd2::mapEventRoomEligible(int(region)))
        return {DomainStatus::Applied,false};
    auto &cursor=state_.roomEvents[{region,index}];
    if(cursor.generation && cursor.generation!=area.generation) return {DomainStatus::Stale,{}};
    cursor.generation=area.generation;
    const int program=state_.instance->controller;
    const int progress=nativeMultiply(processed,100)/pd2::mapEventRoomGoal(int(region));
    if(!cursor.planned) {
        cursor.planned=true;
        const auto &bounds=area.definition.activation.rooms().at(index);
        cursor.center={float(bounds.x+bounds.width/2),float(bounds.y+bounds.height/2)};
        cursor.requested=cursor.center;
        if(ports_.world.nativeProtectedSpawnPosition(region,cursor.center)) cursor.done=true;
        else {
            const auto chance=pd2CrtRandom(ports_.random)%100;
            const auto phase=state_.eventProgress;
            switch(program) {
            case 11:if(!phase && int(chance)+progress>140) cursor.definition=922;break;
            case 12:if(!phase && int(chance)+progress>140) cursor.definition=919;break;
            case 14:if(phase<3 && int((phase+1)*25)<progress && chance<25) cursor.definition=921;break;
            case 15:if(phase<3 && int((phase+1)*25)<progress && chance<25) cursor.definition=945;break;
            case 18:if(!phase && int(chance)+progress>140) {cursor.definition=147;cursor.type=2;}break;
            case 19:if(!phase && int(chance)+progress>140) {cursor.definition=611;cursor.type=1;}break;
            case 20:if((phase&255)<3 && int((phase&255)*20+25)<progress && chance<25) cursor.definition=1182;break;
            case 21:if(!phase && int(chance)+progress>140) {cursor.definition=586;cursor.type=1;}break;
            case 16:
                if(!phase && chance<20 && processed>15) {
                    cursor.definition=593;cursor.type=1;
                    const auto x=pd2CrtRandom(ports_.random)%uint32_t(bounds.width);
                    const auto y=pd2CrtRandom(ports_.random)%uint32_t(bounds.height);
                    cursor.requested={float(bounds.x+int(x)),float(bounds.y+int(y))};
                } else if(phase>1 && phase<50) {
                    const auto type=pd2CrtRandom(ports_.random)%6;
                    if(type>=1 && type<=3) {
                        cursor.definition=type==1?993:type==2?994:995;cursor.phase=4;
                        const auto &group=area.definition.populationRules.classes.at(cursor.definition);
                        const int range=group.maximum-group.minimum+1;
                        cursor.remaining=range>1?group.minimum+int(pd2CrtRandom(ports_.random)%uint32_t(range)):range;
                    }
                }
                break;
            }
            if(cursor.definition<0) cursor.done=true;
        }
    }
    const auto finish=[&]()->DomainResult<bool> {
        if(program==16 && progress>90 && state_.eventProgress>=2 && (state_.eventProgress<50 || state_.taintedEnding)) {
            const auto ended=endTaintedSun(tick);if(!ended) return {ended.status,{}};
        }
        return {DomainStatus::Applied,cursor.skip};
    };
    if(cursor.done) return finish();
    if(cursor.phase==4) {
        while(cursor.remaining>0) {
            if(!cursor.positionPrepared) {
                const int x=int(pd2CrtRandom(ports_.random)%10)-5,y=int(pd2CrtRandom(ports_.random)%10)-5;
                cursor.requested=cursor.center+Vec{float(x),float(y)};
                cursor.position=prepareScenePosition(region,cursor.requested,10);
                cursor.positionPrepared=true;
            }
            if(cursor.position) {
                const auto &point=*cursor.position;
                ports_.world.activate(point.area,point.position);
                if(!ports_.areas.loaded(point.area,point.position)) return {DomainStatus::PendingContent,{}};
                const auto born=ports_.monsters.admitPd2SceneMonster(point.area,point.position,cursor.definition,
                    "map-event."+std::to_string(int(region))+"."+std::to_string(index)+"."+std::to_string(cursor.step),tick.tick);
                if(isDeferred(born.status)) return {born.status,{}};
                if(!born && born.status==DomainStatus::NotImplemented) return {born.status,{}};
            }
            --cursor.remaining;++cursor.step;cursor.positionPrepared=false;cursor.position.reset();
        }
        cursor.done=true;return finish();
    }
    if(cursor.phase==0) {
        if(!cursor.positionPrepared) {
            cursor.position=prepareScenePosition(region,cursor.requested,int(region)==142 || int(region)==144?8:12);
            cursor.positionPrepared=true;
        }
        if(!cursor.position) {cursor.done=true;return finish();}
        const auto &point=*cursor.position;
        ports_.world.activate(point.area,point.position);
        if(!ports_.areas.loaded(point.area,point.position)) return {DomainStatus::PendingContent,{}};
        DomainResult<EntityId> born{DomainStatus::Unavailable,{}};
        if(cursor.type==1) born=ports_.objects.admit({cursor.definition,point.area,point.position,{},{},0,tick.tick});
        else if(cursor.type==2) {
            const auto &npcs=ports_.areas.at(point.area).definition.questNpcs;
            const auto npc=std::find_if(npcs.begin(),npcs.end(),[](const auto &entry){return entry.second.nativeClass==147;});
            if(npc==npcs.end()) return {DomainStatus::NotImplemented,{}};
            born=ports_.world.admitQuestNpc(point.area,npc->first,point.position,0);
        } else born=ports_.monsters.admitPd2SceneMonster(point.area,point.position,cursor.definition,
            "map-event."+std::to_string(int(region))+"."+std::to_string(index),tick.tick);
        if(isDeferred(born.status)) return {born.status,{}};
        if(!born) {
            if(born.status==DomainStatus::NotImplemented) return {born.status,{}};
            cursor.done=true;return finish();
        }
        cursor.source=*born.value;cursor.phase=1;
        if(program!=21) {
            ++state_.eventProgress;cursor.progressCommitted=true;
            if(program==16 || program==19) {
                ports_.world.retainControllerUnitRoom(cursor.source,point.area,point.position,true);
                state_.eventOperations.try_emplace(cursor.source);
            }
        }
    }
    if(cursor.phase==1) {
        const auto &point=*cursor.position;
        if(program==21) {
            if(!cursor.portal.portalObject) {cursor.portal.portalObject=cursor.source;cursor.portal.portalPhase=1;}
            const pd2::MapPortalProgram portal{173,0,586,1,1,{},true};
            const auto linked=advanceDeathPortal(point.area,cursor.portal,portal,586,point.position,0,tick);
            if(!linked) return {linked.status,{}};
            if(!cursor.portal.portalDestination) {cursor.done=true;return finish();}
        }
        const int extra=program==16?3:program==18 || program==21?1:0;
        while(cursor.step<extra) {
            const Vec offset=program!=16?Vec{}:cursor.step==0?Vec{2,0}:cursor.step==1?Vec{0,2}:Vec{-1,-1};
            const auto created=ports_.objects.admit({program==16?374:594,point.area,point.position+offset,{},{},0,tick.tick});
            if(isDeferred(created.status)) return {created.status,{}};
            ++cursor.step;
        }
        if(!cursor.progressCommitted) {++state_.eventProgress;cursor.progressCommitted=true;}
        cursor.phase=2;
    }
    if(cursor.phase==2) {
        const auto sent=notify(noticeKey(program),1,tick);if(!sent) return {sent.status,{}};
        cursor.skip=cursor.done=true;
    }
    return finish();
}
}
