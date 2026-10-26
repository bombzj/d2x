#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/monsters/unique_modifiers.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <cmath>

namespace d2x::server::ai {
bool System::pd2LootWaiting(EntityId id) const {return ports_.loot.read().pending.contains(id);}
StepStatus System::pd2TreasureFallen(EntityId id,std::optional<UnitTarget> target,Vec point,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    const auto &area=ports_.areas.at(unit.area);
    bool resumed=false;
    if(controller.pd2LootPending) {
        if(ports_.loot.read().pending.contains(id)) return StepStatus::Blocked;
        ports_.loot.takeCompletion(id);controller.pd2LootPending=false;resumed=true;
        controller.random=controller.randomExpected=unit.combatRandom;controller.randomPublished=false;
    }
    // 35A180/382410 calls ordinary monster DropItems with a null killer.
    // One invocation catches up at most one of ten missing life deciles.
    const int missing=10-int(std::ceil(double(playerLifePercentage(unit.life,unit.maximumLife))/10.0));
    if(!resumed && controller.phase<missing) {
        controller.phase=nativeAdd(controller.phase,1);
        if(!unit.rule.noTreasure) {
            LootRequest source;source.source=id;source.identity=unit.identity;source.region=unit.area;
            source.difficulty=unit.rule.difficulty;source.sourceSeed=true;
            source.rewardModifiers=monsterRewardModifiers(unit.rule.enchantment);source.monsterPlayerCount=unit.admittedPlayerCount;
            loot::Request request;request.occurrence=tick.tick;request.source=std::move(source);request.position=unit.position;
            request.environment=true;request.reportCompletion=true;
            const auto queued=perform(controller,[this,request] {return ports_.loot.queue(request);});
            if(!queued && !isDeferred(queued.status)) return StepStatus::NotImplemented;
            controller.pd2LootPending=true;
            return StepStatus::Blocked;
        }
    }
    const auto idle=[&](int frames,bool neutral) {
        const auto result=perform(controller,[this,id,neutral] {
            if(neutral) ports_.monsters.neutralMode(id);
            return DomainResult<>{DomainStatus::Applied,std::monostate{}};
        });
        if(neutral) controller.pursuing=false;
        controller.pd2MovingTimer=true;
        controller.nextDecision=tick.tick+uint64_t(frames);
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    if(unit.nativeMode!=1) return idle(10,true);
    if(!target) return idle(10,pd2UnitEventRandom(controller.random)%3!=0);
    const int x=int(std::floor(unit.position.x+area.definition.origin.x));
    const int y=int(std::floor(unit.position.y+area.definition.origin.y));
    const int key=nativeNarrow(uint32_t(x)*uint32_t(y));
    if(key==controller.loop) {
        if(controller.thirdParameter>2 && distance<10) {
            const int tx=int(std::floor(point.x+area.definition.origin.x));
            const int ty=int(std::floor(point.y+area.definition.origin.y));
            const int dx=2+int(pd2UnitEventRandom(controller.random)%3);
            const int dy=2+int(pd2UnitEventRandom(controller.random)%3);
            const Vec destination{float(x+(x<=tx?dx:-dx))-area.definition.origin.x,
                                  float(y+(y<=ty?dy:-dy))-area.definition.origin.y};
            const auto result=usePd2Skill(controller,{id,unit.rule.skillIds[0],{},tick.tick,4,destination});
            controller.thirdParameter=0;controller.pd2MovingTimer=false;return result;
        }
        controller.thirdParameter=nativeAdd(controller.thirdParameter,1);
        return idle(4,false);
    }
    controller.loop=key;controller.thirdParameter=0;
    // Escape(7,0) precedes the final PD2 unit draw. Movement itself uses no
    // seed; preserve that order rather than turning this into pursuit.
    const auto moved=ports_.monsters.requestMove({id,{unit.area,area.generation,monsterRetreatPoint(unit.position,point,7)},
        {},0,75,false,1,0,0,13,7});
    if(isDeferred(moved.status)) return StepStatus::Blocked;
    controller.pursuing=bool(moved);controller.nextDecision=tick.tick;
    controller.pd2MovingTimer=false;
    if(pd2UnitEventRandom(controller.random)&1) return StepStatus::Complete;
    return idle(4,false);
}
}
