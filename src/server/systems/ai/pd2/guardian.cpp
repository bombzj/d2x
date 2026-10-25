#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <array>
#include <cmath>
#include <deque>

namespace d2x::server::ai {
namespace {
std::optional<Vec> guardianPosition(const AreaStore &areas,const monsters::Actor &unit,Vec origin,
                                  const AreaStore::CollisionSample &sample) {
    const auto &area=areas.at(unit.area).definition;
    const auto *bounds=area.activation.room(unit.position+Vec{.5f,.5f});
    if(!bounds) return {};
    std::optional<RoomView> cursor=RoomView{unit.area,(int(area.origin.x)+bounds->x)/5,(int(area.origin.y)+bounds->y)/5};
    std::array<bool,400> visited{};
    std::deque<std::array<int,2>> queue{{10,10}};
    visited[210]=true;
    constexpr std::array<std::array<int,2>,4> neighbours{{{-1,0},{0,1},{1,0},{0,-1}}};
    while(!queue.empty()) {
        const auto node=queue.front();queue.pop_front();
        const int x=int(origin.x)+(node[0]-10)*4,y=int(origin.y)+(node[1]-10)*4;
        if(!cursor) return {}; // Native 387BB0 replaces its room cursor, even on failure.
        const auto &current=areas.at(cursor->area).definition;
        cursor=areas.roomAtPosition(cursor->area,Vec{float(cursor->x*5),float(cursor->y*5)}-current.origin,x,y);
        bool blocked=!cursor;
        if(cursor) {
            const auto &candidate=areas.at(cursor->area).definition;
            const Vec anchor=Vec{float(cursor->x*5),float(cursor->y*5)}-candidate.origin;
            // 387BB0 calls the rectangular 2x2 query. Native size-2's
            // five-cell cross is a different collision footprint.
            for(int dy=-1;dy<=0;++dy) for(int dx=-1;dx<=0;++dx) {
                const auto room=areas.roomAtPosition(cursor->area,anchor,x+dx,y+dy);
                if(!room || (sample(*room,x+dx,y+dy)&0x101)) blocked=true;
            }
        }
        if(!blocked) return Vec{float(x),float(y)}-area.origin;
        for(const auto offset:neighbours) {
            const int nx=node[0]+offset[0],ny=node[1]+offset[1];
            // Guardian passes radius 4 to 387C10: [6,14), not a disk.
            if(nx<6 || nx>=14 || ny<6 || ny>=14) continue;
            const auto index=size_t(nx*20+ny);
            if(visited[index]) continue;
            visited[index]=true;queue.push_back({nx,ny});
        }
    }
    return {};
}
}
StepStatus System::pd2Guardian(EntityId id,UnitTarget target,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    const auto &area=ports_.areas.at(unit.area).definition;
    auto &blinkPending=controller.phase;
    auto &summonChance=controller.loop;
    auto &summoned=controller.thirdParameter;
    const auto cast=[&](size_t slot,UnitTarget victim,std::optional<Vec> position={}) {
        const auto skill=unit.rule.skillIds[slot];
        const auto &action=unit.rule.skillActions.at(skill);
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,action.nativeMode,position});
        controller.nextDecision=tick.tick+1;
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto blink=[&]()->std::optional<StepStatus> {
        // The imported CRT stream is shared with other PD2 rand callers;
        // neither the ordinary unit SEED nor 36DB70 participates here.
        const int x=int(std::floor(unit.position.x+.5f+area.origin.x))+int(pd2CrtRandom(ports_.pd2Random)%16)-8;
        const int y=int(std::floor(unit.position.y+.5f+area.origin.y))+int(pd2CrtRandom(ports_.pd2Random)%16)-8;
        const auto point=guardianPosition(ports_.areas,unit,{float(x),float(y)},ports_.spatial.nativeCollisionSample(0x101));
        if(!point) return {};
        return cast(0,{},*point);
    };
    if(blinkPending==1) {
        blinkPending=0;
        if(const auto result=blink()) return *result;
    }
    const int roll=int(pd2CrtRandom(ports_.pd2Random)%100);
    const bool canSummon=unit.rule.nativeClass==1064 && summoned<6;
    if(roll<summonChance) {
        if(canSummon) {
            const auto result=cast(2,target);
            blinkPending=1;summonChance=0;++summoned;
            return result;
        }
    } else if(canSummon) summonChance=nativeAdd(summonChance,6-summoned);
    if(roll<80) return cast(1,target);
    if(roll<95) if(const auto result=blink()) return *result;
    ports_.monsters.stop(id);controller.pursuing=false;
    controller.nextDecision=tick.tick+uint64_t(unit.rule.neutralDelayTicks);
    return StepStatus::Complete;
}
}
