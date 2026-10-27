#include "../system.hpp"
#include "scene_pack.hpp"
#include "../native_birth.hpp"
#include "server/area_store.hpp"
#include "gameplay/monsters/unique_modifiers.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::monsters {
DomainResult<> System::advancePd2ScenePack(Pd2ScenePack &pack,RegionId region,Vec position,int definition,
    MonsterRank rank,std::string key,uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    if(pack.done()) return {DomainStatus::Applied,std::monostate{}};
    const auto *area=ports_.areas.find(region);if(!area || !area->definition.pd2Map) return {DomainStatus::InvalidRequest,{}};
    const auto spawn=[&](int type,Vec point,int mode,int attempts,EntityId owner,std::string suffix)->DomainResult<EntityId> {
        for(const auto &[id,actor]:state_.actors)
            if(actor.area==region && actor.identity.spawnKey==key+suffix) return {DomainStatus::Applied,id};
        const auto prototype=area->definition.nativeMonsterClasses.find(type);
        if(prototype==area->definition.nativeMonsterClasses.end()) return {DomainStatus::NotImplemented,{}};
        const auto global=point+area->definition.origin;
        const auto room=ports_.areas.roomAtPosition(region,point,int(global.x),int(global.y));
        if(!room) return {DomainStatus::Applied,EntityId{}};
        const auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
        auto identity=prototype->second.identity;identity.spawnKey=key+suffix;identity.origin=SpawnOrigin::Summoned;
        identity.rank=MonsterRank::Normal;identity.ownerSpawnKey.clear();
        if(owner) {
            const auto *leader=find(owner);if(!leader) return {DomainStatus::Stale,{}};
            identity.ownerSpawnKey=leader->identity.spawnKey;
        }
        Admission request{identity,prototype->second.implementation,room->area,point,true,prototype->second.rule,{},tick};
        request.minionOwner=owner;request.ownsMinions=!owner && rank!=MonsterRank::Normal;
        if(mode==8) {
            const auto animation=area->definition.pd2MissileSpawns.find(type);
            if(animation==area->definition.pd2MissileSpawns.end() || animation->second.skillOneTicks<=0) return {DomainStatus::NotImplemented,{}};
            request.birthTicks=animation->second.skillOneTicks;
        }
        const auto birth=prepareRoomSpawn(request,*loan.value,attempts,{},false,collision);
        if(!birth) return {birth.status,{}};
        if(!(*birth.value)->validate()) return {DomainStatus::Stale,{}};
        const auto id=(*birth.value)->actor();(*birth.value)->publish();
        return {DomainStatus::Applied,id};
    };
    if(!pack.phase) {
        const auto born=spawn(definition,position,8,-1,{},".leader");if(!born) return {born.status,{}};
        pack.leader=*born.value;
        if(!pack.leader || rank==MonsterRank::Normal) {pack.phase=7;return {DomainStatus::Applied,std::monostate{}};}
        pack.phase=1;
    }
    if(pack.phase==1) {
        const auto *leader=find(pack.leader);if(!leader || !leader->rule.modifierChoices) return {DomainStatus::NotImplemented,{}};
        auto random=leader->combatRandom;
        const auto &choices=*leader->rule.modifierChoices;
        const bool champion=rank==MonsterRank::Champion;
        const int count=champion?1:std::min(9-int(leader->rule.nativeModifiers.size()),leader->rule.difficulty+int(limitedRandom(random,1))+1);
        pack.modifiers=chooseMonsterModifiers(choices.candidates[champion?1:0],leader->rule.nativeModifiers,count,random);
        pack.remaining=champion?0:3+int(limitedRandom(random,4));
        const auto consumed=consumeRandom(leader->id,leader->combatRandom,random);if(!consumed) return consumed;
        pack.phase=2;
    }
    if(pack.phase==2) {
        const auto *leader=find(pack.leader);if(!leader) return {DomainStatus::InvalidActor,{}};
        const auto &family=area->definition.populationRules.classes.at(definition);
        const int minion=family.minions[0]>=0?family.minions[0]:definition;
        while(pack.remaining>0) {
            const auto born=spawn(minion,leader->position,1,3,pack.leader,".minion."+std::to_string(pack.remaining));
            if(!born) return {born.status,{}};
            if(*born.value) pack.minions.push_back(*born.value);
            --pack.remaining;
        }
        pack.phase=3;
    }
    if(pack.phase==3) {
        if(modifiersPending()) return {DomainStatus::PendingContent,{}};
        const auto prepared=prepareBirthModifiers(pack.leader,rank,pack.modifiers,pack.minions,tick,{},true);
        if(!prepared) return prepared;
        pack.phase=4;return {DomainStatus::PendingContent,{}};
    }
    if(pack.phase==4) {
        if(modifiersPending()) return {DomainStatus::PendingContent,{}};
        if(rank!=MonsterRank::Champion) {pack.phase=7;return {DomainStatus::Applied,std::monostate{}};}
        const auto *leader=find(pack.leader);if(!leader) return {DomainStatus::InvalidActor,{}};
        auto random=leader->combatRandom;pack.remaining=1+int(limitedRandom(random,3));
        const auto consumed=consumeRandom(leader->id,leader->combatRandom,random);if(!consumed) return consumed;
        pack.phase=5;
    }
    while(pack.remaining>0) {
        if(pack.phase==5) {
            const auto *leader=find(pack.leader);if(!leader) return {DomainStatus::InvalidActor,{}};
            const auto born=spawn(definition,leader->position,1,4,{},".champion."+std::to_string(pack.remaining));
            if(!born) return {born.status,{}};
            pack.champion=*born.value;
            if(!pack.champion) {--pack.remaining;continue;}
            const auto *child=find(pack.champion);
            // 3A2250 -> Game47880 appends UMod16 to each additional
            // champion. Only the first unit rolls the champion variation.
            auto modifiers=child->rule.nativeModifiers;if(modifiers.size()<9) modifiers.push_back(16);
            const auto prepared=prepareBirthModifiers(child->id,MonsterRank::Champion,std::move(modifiers),{},tick);
            if(!prepared) return prepared;
            pack.phase=6;return {DomainStatus::PendingContent,{}};
        }
        if(modifiersPending()) return {DomainStatus::PendingContent,{}};
        --pack.remaining;pack.phase=5;
    }
    pack.phase=7;return {DomainStatus::Applied,std::monostate{}};
}
}
