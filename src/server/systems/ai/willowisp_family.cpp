#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace d2x::server::ai {
StepStatus System::willowispFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    const auto &params=unit.rule.ai.params;auto &random=controller.random;
    const auto chance=[&](int percent) {return int(limitedRandom(random,100))<percent;};
    const auto idle=[&](int frames) {
        ports_.monsters.stop(id);controller.pursuing=false;
        ports_.monsters.aiPose(id,1,false,false,tick.tick,frames);
        controller.nextDecision=tick.tick+uint64_t(std::max(1,frames));
    };
    const auto move=[&](Vec goal,EntityId victim={},int steps=1) {
        const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,goal},victim,0,75,false,steps});
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    };
    const auto cast=[&](uint8_t mode,std::optional<Vec> coordinates={}) {
        const auto victim=target.value_or(UnitTarget{{},0,0});
        const auto result=requestAction(controller,{id,0,victim,tick.tick,mode,coordinates});
        controller.nextDecision=tick.tick+(!result?10:1);
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto squared=[&](Vec at) {
        const int x=int(std::floor(unit.position.x))-int(std::floor(at.x));
        const int y=int(std::floor(unit.position.y))-int(std::floor(at.y));return x*x+y*y;
    };
    if(target && int32_t(uint32_t(tick.tick))>controller.thirdParameter && squared(point)<1024 && controller.phase<4 &&
       limitedRandom(random,1000)<=uint32_t(unit.rule.difficulty+2)) controller.phase=4;
    if(controller.phase==4) {
        std::vector<EntityId> wisps;
        if(const auto *room=area.definition.activation.room(unit.position)) {
            const auto inspect=[&](RoomView near) {
                for(const auto candidate:ports_.areas.roomUnitOrder(near))
                    if(const auto *other=ports_.monsters.find(candidate);other && other->alive() && other->rule.baseClass==unit.rule.baseClass)
                        wisps.push_back(candidate);
            };
            // UNITFINDS' explicit callback replaces its ordinary filter and
            // radius test. Only its choice of current/adjacent rooms uses 32.
            if(unit.position.x-32>room->x && unit.position.y-32>room->y &&
               unit.position.x+32<room->x+room->width && unit.position.y+32<room->y+room->height)
                inspect({unit.area,(int(area.definition.origin.x)+room->x)/5,(int(area.definition.origin.y)+room->y)/5});
            else for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data()))) inspect(near);
        }
        const int wait=337-int(tick.tick%337);
        if(wisps.size()==4 || wisps.size()==5) {
            for(size_t index=0;index<wisps.size();++index) {
                const auto *other=ports_.monsters.find(wisps[index]);
                auto [entry,fresh]=state_.controllers.try_emplace(wisps[index]);auto &peer=entry->second;
                if(fresh) {peer.actor=other->id;peer.admissionRevision=other->admissionRevision;peer.random=other->combatRandom;}
                peer.phase=wisps.size()==4?6:5;peer.loop=0;peer.thirdParameter=int32_t(index+1);peer.pursuing=false;
                peer.nextDecision=tick.tick+uint64_t(wait);
                ports_.skills.cancel({},other->id);ports_.monsters.aiPose(other->id,1,false,false,tick.tick,wait);
                ports_.monsters.scheduleAi(other->id,peer.nextDecision);
            }
            idle(wait);
        } else {controller.phase=2;idle(8);}
        return StepStatus::Complete;
    }
    if(controller.phase>=5) {
        const bool baptism=controller.phase==5;
        const int index=controller.thirdParameter-1,shape=controller.phase-6;
        if(index<0 || index>=(baptism?5:4) || (!baptism && (shape<0 || shape>=7))) {
            controller.phase=2;idle(8);return StepStatus::Complete;
        }
        static constexpr std::array<Vec,5> baptismPositions{{{-5,-5},{3,-5},{6,3},{3,6},{-5,3}}};
        static constexpr std::array<Vec,5> baptismTargets{{{6,3},{3,6},{-5,3},{-5,-5},{3,-5}}};
        static constexpr std::array<std::array<Vec,4>,7> positions{{
            {{{-10,-3},{1,-5},{6,0},{4,10}}}, {{{-9,-2},{-6,-11},{-2,-5},{6,1}}},
            {{{0,13},{-12,0},{7,5},{-3,-11}}}, {{{-15,-3},{-6,-3},{-4,-5},{-6,-13}}},
            {{{-12,-2},{-6,2},{-5,5},{1,11}}}, {{{-13,-6},{-8,-11},{-7,0},{1,8}}},
            {{{-5,-8},{-8,1},{1,-6},{6,9}}}}};
        static constexpr std::array<std::array<Vec,4>,7> targets{{
            {{{1,-5},{6,0},{4,10},{-10,-3}}}, {{{1,8},{-9,-2},{-5,4},{1,8}}},
            {{{-12,0},{7,5},{-3,-11},{11,-2}}}, {{{7,9},{-15,-3},{-6,-13},{9,7}}},
            {{{-5,-7},{-5,-7},{9,0},{-12,-2}}}, {{{-8,-11},{-2,-6},{-2,-6},{-13,-6}}},
            {{{-8,1},{1,-6},{6,9},{0,0}}}}};
        const Vec anchor{std::floor(point.x),std::floor(point.y)};
        const Vec destination=anchor+(baptism?baptismPositions[size_t(index)]:positions[size_t(shape)][size_t(index)]);
        if(monsterAiDistance(destination,0,unit.position)>3) {
            if(chance(35)) move(monsterWanderPoint(unit.position,4,random));else move(destination+Vec{.5f,.5f},{},0);
            return StepStatus::Complete;
        }
        if(const auto frame=int(tick.tick%67)) {idle(67-frame);return StepStatus::Complete;}
        if(controller.loop>=3) {
            if(baptism) controller.phase=2;
            else {controller.loop=0;if(++controller.phase>12) controller.phase=2;}
            if(controller.phase==2) controller.thirdParameter=int32_t(uint32_t(tick.tick+1800));
            idle(337-int(tick.tick%337));
            if(!baptism && target && target->type==0) if(const auto *player=ports_.players.findActor(target->id);player && player->persistent.player.hp>0) {
                CombatEffectSpec effect;effect.nativeDirectAllocation=true;effect.nativeValues.emplace();
                effect.stacking=EffectStacking::Independent;effect.duration=24*60*60*20;
                effect.source={CombatEffectSource::Monster,id,0,unit.rule.difficulty+1};
                const std::array<StatValue,1> writes{{{StatKey{unit.rule.stats->names.at("item_magicbonus"),0},50*(unit.rule.difficulty+1)}}};
                const ActorContext recipient{player->player,player->actor,player->area,ports_.areas.at(player->area).generation,0,tick.tick};
                const auto applied=perform(controller,[this,recipient,effect,writes]{return ports_.effects.apply(recipient,effect,false,writes);});
                if(isDeferred(applied.status)) return StepStatus::Blocked;
            }
            return StepStatus::Complete;
        }
        const auto offset=baptism?baptismTargets[size_t(index)]:targets[size_t(shape)][size_t(index)];
        ++controller.loop;
        if(offset.x==0 && offset.y==0) {idle(67-int(tick.tick%67));return StepStatus::Complete;}
        return cast(7,anchor+offset+Vec{.5f,.5f});
    }
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange &&
        area.definition.collision.missileSegment(unit.position,point,{4,1});
    if(controller.phase==3) {
        if(controller.loop<=0 && (contact || chance(params[0]))) {
            controller.phase=contact?1:2;
            const auto emerged=perform(controller,[this,id,tick]{return ports_.monsters.aiPose(id,8,false,false,tick.tick);});
            controller.nextDecision=tick.tick+1;return isDeferred(emerged.status)?StepStatus::Blocked:StepStatus::Complete;
        }
        --controller.loop;
        if(chance(params[2])) move(point,target?target->id:EntityId{});else move(monsterWanderPoint(unit.position,6,random));
    } else {
        if(contact && (controller.phase==1 || chance(params[1]))) {controller.phase=0;return cast(4);}
        if(!contact && (controller.phase==2 || chance(params[0]))) {controller.phase=0;return cast(7,target?std::nullopt:std::optional{point});}
        controller.phase=3;controller.loop=3;
        if(chance(params[2])) move(point,target?target->id:EntityId{});else move(monsterWanderPoint(unit.position,4,random));
    }
    return StepStatus::Complete;
}
}
