#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/quests/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
StepStatus System::baalThroneFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area).definition;
    const auto idle=[&](uint64_t frames) {ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+frames;};
    if(nativeNarrow(uint32_t(tick.tick))<controller.thirdParameter) {
        // Fn134 really passes frame-deadline, including the negative value.
        // Event::InitTimer clamps the resulting past frame to current+1.
        idle(1);return StepStatus::Complete;
    }
    bool occupied=false;
    if(const auto *room=area.activation.room(unit.position))
        for(const auto near:area.roomNear.at(size_t(room-area.activation.rooms().data())))
            for(const auto candidate:ports_.areas.roomUnitOrder(near)) {
                const auto *other=ports_.monsters.find(candidate);
                if(!other || candidate==id || !other->alive() || !sameAlignment(unit.alignment(),other->alignment())) continue;
                const auto point=other->position+ports_.areas.at(other->area).definition.origin-area.origin;
                if(monsterAiDistance(unit.position,unit.rule.size,point)<64) occupied=true;
            }
    if(occupied) {
        const auto target=acquireTarget(unit,tick,false,false);
        if(target && !ports_.effects.unitStates(target->unit.id,tick.tick).contains(unit.rule.closeCurseState) &&
           int(limitedRandom(controller.random,100))<unit.rule.ai.params[0]) {
            const auto skill=unit.rule.skillIds[0];const auto found=unit.rule.skillActions.find(skill);
            if(found!=unit.rule.skillActions.end()) {
                const auto cast=requestAction(controller,{id,skill,target->unit,tick.tick,found->second.nativeMode});
                controller.nextDecision=tick.tick+1;return isDeferred(cast.status)?StepStatus::Blocked:StepStatus::Complete;
            }
        }
        idle(10);return StepStatus::Complete;
    }
    const auto result=perform(controller,[this,id,tick,wave=unit.baalThroneWave,spawn=bool(controller.loop&1),hinted=false,assigned=false,casted=false]() mutable ->DomainResult<> {
        auto &ai=state_.controllers.at(id);
        const auto *unit=ports_.monsters.find(id);if(!unit || !unit->alive()) return {DomainStatus::InvalidActor,{}};
        const auto &area=ports_.areas.at(unit->area);
        ai.nextDecision=tick.tick+1;
        if(spawn && wave>=5) {
            const auto changed=ports_.monsters.reinitializeBaalToStairs(id,tick.tick);
            if(changed) ai.nextDecision=tick.tick+5;
            return changed;
        }
        if(!spawn && wave>=0 && wave<5 && !hinted) {
            const auto sent=ports_.quests.baalThroneClassHints(id,unsigned(wave),tick.tick);if(!sent) return sent;
            hinted=true;
        }
        if(spawn && !assigned) {
            const auto preset=area.definition.nativeQuestMonsters.find("Baal Subject "+std::to_string(wave+1));
            if(preset==area.definition.nativeQuestMonsters.end()) return {DomainStatus::NotImplemented,{}};
            const auto learned=ports_.monsters.assignBaalMonsterSpawn(id,preset->second.mappedClass,tick.tick);if(!learned) return learned;
            assigned=true;unit=ports_.monsters.find(id);
        }
        if(!casted) {
            const auto name=spawn?"baal monster spawn":"baal corpse explode";
            const auto skill=unit->rule.formulas->skillIds.find(name);
            if(skill==unit->rule.formulas->skillIds.end()) return {DomainStatus::NotImplemented,{}};
            const auto action=unit->rule.skillActions.find(skill->second);
            if(action==unit->rule.skillActions.end()) return {DomainStatus::NotImplemented,{}};
            actions::MonsterCastRequest request{id,uint16_t(skill->second),spawn?UnitTarget{}:UnitTarget{id,0,1},tick.tick,action->second.nativeMode};
            if(spawn) request.position=unit->position+Vec{0,13};
            const auto cast=ports_.skills.requestCast(request);if(isDeferred(cast.status)) return cast;
            casted=true;
        }
        if(spawn) {
            const auto advanced=ports_.monsters.setBaalThroneWave(id,wave,wave+1);if(!advanced) return advanced;
            ai.loop=(ai.loop|2)&~1;ai.thirdParameter=nativeAdd(nativeNarrow(uint32_t(tick.tick)),100);
        }
        else {
            const auto sound=ports_.quests.baalThroneSound(id,tick.tick);if(!sound) return sound;
            ai.loop|=1;ai.thirdParameter=nativeAdd(nativeNarrow(uint32_t(tick.tick)),250);
        }
        return {DomainStatus::Applied,std::monostate{}};
    });
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
