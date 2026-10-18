#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::ai {
StepStatus System::succubusFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);const auto &params=unit.rule.ai.params;
    const bool witch=unit.rule.ai.kind==MonsterAiKind::SuccubusWitch;
    const auto chance=[&](int percent){return int(limitedRandom(controller.random,100))<percent;};
    const auto has=[&](size_t slot){return unit.rule.skillIds[slot] && unit.rule.skillActions.contains(unit.rule.skillIds[slot]);};
    const auto cast=[&](int slot,UnitTarget victim) {
        const auto skill=slot<0?uint16_t{}:unit.rule.skillIds[size_t(slot)];
        const auto mode=slot<0?uint8_t{4}:unit.rule.skillActions.at(skill).nativeMode;
        const auto result=requestAction(controller,{id,skill,victim,tick.tick,mode});
        controller.nextDecision=tick.tick+(result?1:10);return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    const auto primary=target.value_or(UnitTarget{});
    const auto recipient=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(primary.id);
    const auto *effects=ports_.effects.unitEffects(primary.id);
    const bool cursed=effects && std::ranges::any_of(effects->entries(),[&](const auto &entry){
        return entry.activeAt(tick.tick) && entry.spec.state.curse && !entry.spec.nativeDirectAllocation;
    });
    if(!cursed && distance<params[3] && chance(params[2])) {
        const auto targetLife=recipient.player?recipient.player->persistent.player.hp.raw():recipient.monster?recipient.monster->life:0;
        const auto maximumLife=recipient.player?recipient.player->totals.character.maxLifeFixed.raw():recipient.monster?recipient.monster->maximumLife:0;
        if(has(0) && playerLifePercentage(targetLife,maximumLife)>=params[6]) return cast(0,primary);
        if(has(1) && playerLifePercentage(unit.life,unit.maximumLife)<=params[7]) return cast(1,primary);
        int64_t maximumMana=recipient.player?recipient.player->totals.character.maxManaFixed.raw():0;
        if(recipient.monster && recipient.monster->rule.stats) if(const auto stats=ports_.effects.unitStatValues(primary.id,tick.tick))
            maximumMana=nativeStatValue(*stats,*recipient.monster->rule.stats,"maxmana");
        if(has(2) && (maximumLife>maximumMana || !recipient.player)) return cast(2,primary);
        if(has(3) && recipient.player) return cast(3,primary);
    }
    const bool contact=target && meleeDistance(unit.position,unit.rule.size,point,size)<=unit.rule.meleeRange && area.definition.collision.missileSegment(unit.position,point,{4,1});
    const auto idle=[&](int delay){ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(delay);};
    if(witch) {
        // Escape and circling do not consume the unit seed in requestMove.
        // Failed movement must precede the next AI roll, without publishing
        // an earlier seed which a later decision would overwrite.
        const auto move=[&](Vec at,EntityId victim,int turn=0,int circle=0,uint8_t path=13,uint8_t bound=5) {
            const auto result=ports_.monsters.requestMove({id,{unit.area,area.generation,at},victim,0,75,false,1,turn,circle,path,bound});
            controller.pursuing=bool(result);controller.nextDecision=tick.tick+(result?1:10);return bool(result);
        };
        const auto escape=[&] {
            const auto range=uint8_t(params[3]);
            return target && move(monsterRetreatPoint(unit.position,point,range),{},0,0,2,range>5?std::min<uint8_t>(range,77):5);
        };
        if(contact) {
            if(chance(params[2]) && escape()) return StepStatus::Complete;
            if(chance(params[0])) return cast(-1,primary);
            idle(params[5]);return StepStatus::Complete;
        }
        if(has(4) && params[7]>0 && chance(params[4])) {
            const auto ranged=shootableTarget(unit,tick);
            distance=ranged?ranged->distance:INT32_MAX;
            if(ranged) {
                if(chance(params[7])) return cast(4,ranged->unit);
            }
        }
        if(distance<params[3] && chance(params[2]) && escape()) return StepStatus::Complete;
        // Every current Witch declares skill5. Refuse to synthesize an S2
        // attack program for a future content row without that declaration.
        if(!has(4)) return StepStatus::Complete;
        if(chance(params[1])) {move(point,primary.id);return StepStatus::Complete;}
        if(chance(50)) {
            const int turn=(rollRandom(controller.random)&255)<128?1:-1;
            if(move(point,primary.id,turn,6)) return StepStatus::Complete;
        }
        idle(params[5]);return StepStatus::Complete;
    }
    if(contact) {
        if(chance(params[0])) return cast(-1,primary);
        idle(params[4]);return StepStatus::Complete;
    }
    // Normal's aip8 is empty. Nightmare/Hell's own 50/80 gates enable
    // this independent shootable-target search and its subsequent roll.
    if(has(4) && params[7]>0) if(const auto ranged=shootableTarget(unit,tick);ranged && chance(params[7])) return cast(4,ranged->unit);
    if(chance(params[1])) {
        const auto moved=perform(controller,[this,id,region=unit.area,generation=area.generation,point,primary] {
            return ports_.monsters.requestMove({id,{region,generation,point},primary.id,0,75,false,1});
        });
        controller.pursuing=bool(moved);controller.nextDecision=tick.tick+(moved?1:10);
    } else idle(params[5]);
    return StepStatus::Complete;
}
}
