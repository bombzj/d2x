#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"

namespace d2x::server::ai {
StepStatus System::madawcFamily(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    const auto idle=[&](int frames) {ports_.monsters.neutralMode(id);controller.pursuing=false;controller.nextDecision=tick.tick+uint64_t(frames);};
    if(!ports_.quests.read().actFive.ancientsActive) {idle(25);return StepStatus::Complete;}
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto selected=acquireNativeBossTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},unit,[&](EntityId candidate) {
        const auto other=participants.find(candidate);
        return other.id() && (other.player?other.player->area:other.monster->area)==unit.area?1:0;
    });
    if(!selected.target) {idle(25);return StepStatus::Complete;}
    const auto target=selected.target->unit;const auto point=selected.target->position;
    const int size=selected.target->size,distance=meleeDistance(unit.position,unit.rule.size,point,size);
    const auto result=perform(controller,[this,id,target,point,size,distance,tick,stage=0,
        pending=std::optional<actions::MonsterCastRequest>{}]() mutable ->DomainResult<> {
        auto &ai=state_.controllers.at(id);
        const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
        for(;;) {
            const auto *source=ports_.monsters.find(id);if(!source || !source->alive()) return {DomainStatus::InvalidActor,{}};
            if(pending) {
                const auto cast=ports_.skills.requestCast(*pending);if(isDeferred(cast.status)) return cast;
                pending.reset();ai.nextDecision=tick.tick+1;if(cast) return done();
                continue;
            }
            const auto &area=ports_.areas.at(source->area);const auto &params=source->rule.ai.params;
            const auto chance=[&](int percent)->DomainResult<bool> {
                const auto expected=source->combatRandom;auto random=expected;const bool chosen=int(limitedRandom(random,100))<percent;
                const auto consumed=ports_.monsters.consumeRandom(id,expected,random);return {consumed.status,consumed?std::optional{chosen}:std::nullopt};
            };
            const auto wait=[&](int frames,bool neutral) {
                if(neutral) ports_.monsters.neutralMode(id);
                ai.nextDecision=tick.tick+uint64_t(frames);
            };
            switch(stage) {
            case 0:
                if(unitMeleeReach(area.definition.collision,source->position,source->rule.size,point,size,source->rule.meleeRange)) {
                    const auto escape=chance(params[3]);if(!escape) return {escape.status,{}};
                    if(*escape.value) {
                        const auto moved=ports_.monsters.requestMove({id,{source->area,area.generation,monsterRetreatPoint(source->position,point,params[4])},{},0,75,false,1,0,0,2,5});
                        ai.pursuing=bool(moved);if(moved) {wait(25,false);return done();}
                    }
                }
                stage=1;continue;
            case 1: {
                const auto skill=source->rule.skillIds[0];const auto program=source->rule.formulas->skills.find(skill);
                if(program!=source->rule.formulas->skills.end()) {
                    const auto &row=program->second;
                    const bool ownerState=row.auraState>0 && source->rule.states->contains(row.auraState) && ports_.effects.unitStates(id,tick.tick).contains(row.auraState);
                    const bool targetState=row.auraTargetState>0 && source->rule.states->contains(row.auraTargetState) && ports_.effects.unitStates(target.id,tick.tick).contains(row.auraTargetState);
                    if(!ownerState && !targetState) {
                        const auto shout=chance(params[2]);if(!shout) return {shout.status,{}};
                        if(*shout.value) pending=actions::MonsterCastRequest{id,skill,UnitTarget{},tick.tick,source->rule.skillActions.at(skill).nativeMode};
                    }
                }
                stage=2;continue;
            }
            case 2:
                if(distance<params[0]) {
                    const auto attack=chance(params[1]);if(!attack) return {attack.status,{}};
                    if(*attack.value) pending=actions::MonsterCastRequest{id,0,target,tick.tick,4};
                }
                stage=3;continue;
            default: {
                // PATH_SetStepNum narrows the signed difference to a byte.
                const auto moved=ports_.monsters.requestMove({id,{source->area,area.generation,point},target.id,0,75,false,uint8_t(distance-params[0])});
                ai.pursuing=bool(moved);wait(moved?1:10,!moved);return done();
            }
            }
        }
    });
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
