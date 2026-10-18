#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "core/random.hpp"
#include <cmath>

namespace d2x::server::ai {
StepStatus System::nihlathakFamily(EntityId id,std::optional<UnitTarget> target,Vec point,int size,int distance,TickContext tick,Controller &controller) {
    // Keep the continuation across admission backpressure. A failed corpse
    // cast resumes the callback on the seed published by that cast.
    const auto result=perform(controller,[this,id,target,point,size,distance,tick,stage=0,
        pending=std::optional<actions::MonsterCastRequest>{},pendingClass=std::optional<int>{},terminal=true]() mutable ->DomainResult<> {
        auto &ai=state_.controllers.at(id);
        const auto idle=[&](int delay) {
            ports_.monsters.neutralMode(id);ai.pursuing=false;ai.nextDecision=tick.tick+uint64_t(delay);
        };
        for(;;) {
            const auto *unit=ports_.monsters.find(id);if(!unit || !unit->alive()) return {DomainStatus::InvalidActor,{}};
            if(pendingClass) {
                const auto saved=ports_.monsters.setMinionSpawnClass(id,*pendingClass);if(!saved) return saved;
                pendingClass.reset();
            }
            if(pending) {
                const auto cast=ports_.skills.requestCast(*pending);if(isDeferred(cast.status)) return cast;
                pending.reset();ai.nextDecision=tick.tick+1;
                if(cast || terminal) return {DomainStatus::Applied,std::monostate{}};
                continue;
            }
            const auto &area=ports_.areas.at(unit->area);const auto &params=unit->rule.ai.params;
            const auto stats=ports_.effects.unitStatValues(id,tick.tick);if(!stats) return {DomainStatus::Unavailable,{}};
            const auto ranks=nativeMonsterSkillRanks(unit->rule.baseSkillRanks,*stats,*unit->rule.stats,*unit->rule.formulas);
            const auto has=[&](size_t slot) {
                const auto skill=unit->rule.skillIds[slot];const auto found=ranks.find(skill);
                return skill>0 && unit->rule.skillActions.contains(skill) && found!=ranks.end() && found->second>0;
            };
            const auto cast=[&](size_t slot,UnitTarget victim,bool stop=true) {
                const auto skill=unit->rule.skillIds[slot];
                pending=actions::MonsterCastRequest{id,skill,std::move(victim),tick.tick,unit->rule.skillActions.at(skill).nativeMode};terminal=stop;
            };
            const auto roll=[&](uint32_t bound)->DomainResult<int> {
                const auto expected=unit->combatRandom;auto random=expected;const int value=int(limitedRandom(random,bound));
                const auto consumed=ports_.monsters.consumeRandom(id,expected,random);return {consumed.status,consumed?std::optional{value}:std::nullopt};
            };
            const auto move=[&](Vec at,EntityId victim={},uint8_t path=13) {
                const auto moved=ports_.monsters.requestMove({id,{unit->area,area.generation,at},victim,0,75,false,1,0,0,path,5});
                ai.pursuing=bool(moved);ai.nextDecision=tick.tick+1;return moved;
            };
            const auto wander=[&]()->DomainResult<> {
                const auto expected=unit->combatRandom;auto random=expected;const auto at=monsterWanderPoint(unit->position,6,random);
                const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;
                return move(at);
            };
            switch(stage) {
            case 0: {
                const auto activated=ports_.quests.activateNihlathak(tick.tick);if(isDeferred(activated.status)) return activated;
                if(ports_.effects.unitStates(id,tick.tick).contains(unit->rule.formulas->infernoState)) {
                    const auto cleared=ports_.monsters.toggleAiState(id,unit->rule.formulas->infernoState,false,tick.tick);if(!cleared) return cleared;
                }
                stage=1;continue;
            }
            case 1: {
                bool teleport=!target && has(0) && (unit->nativeAiState==3 || unit->nativeAiState==19);
                if(target && has(0) && unitMeleeReach(area.definition.collision,unit->position,unit->rule.size,point,size,unit->rule.meleeRange)) {
                    const auto chance=roll(100);if(!chance) return {chance.status,{}};teleport=*chance.value<params[0];
                }
                if(teleport) {
                    const auto expected=unit->combatRandom;auto random=expected;const auto range=uint32_t(params[1]*2);
                    const auto at=unit->position+Vec{float(int(limitedRandom(random,range))-params[1]),float(int(limitedRandom(random,range))-params[1])};
                    const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;
                    cast(0,{});pending->position=at;stage=2;continue;
                }
                if(!target) {idle(25);return {DomainStatus::Applied,std::monostate{}};}
                stage=2;continue;
            }
            case 2:
                if(distance<params[4]) {
                    const auto chance=roll(100);if(!chance) return {chance.status,{}};
                    if(*chance.value<40 && move(monsterRetreatPoint(unit->position,point,5),{},2)) return {DomainStatus::Applied,std::monostate{}};
                }
                stage=3;continue;
            case 3: {
                if(has(2)) {
                    const auto chance=roll(100);if(!chance) return {chance.status,{}};
                    if(*chance.value<params[2]) {
                        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};const auto primary=participants.find(target->id);
                        if(primary.id()) {
                            const auto region=primary.player?primary.player->area:primary.monster->area;
                            const auto &definition=ports_.areas.at(region).definition;const auto at=primary.position();
                            if(const auto *room=definition.activation.room(at)) {
                                const auto index=size_t(room-definition.activation.rooms().data());
                                const int centerX=int(std::floor(at.x)),centerY=int(std::floor(at.y));
                                const bool inside=centerX-10>room->x && centerY-10>room->y && centerX+10<room->x+room->width && centerY+10<room->y+room->height;
                                const auto rooms=inside?std::vector<RoomView>{{region,(int(definition.origin.x)+room->x)/5,(int(definition.origin.y)+room->y)/5}}:definition.roomNear.at(index);
                                for(const auto near:rooms) {
                                    for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(const auto *corpse=ports_.monsters.find(candidate);corpse && ports_.monsters.nativeUsableCorpse(candidate,tick.tick)) {
                                        const auto position=corpse->position+ports_.areas.at(corpse->area).definition.origin-definition.origin;
                                        const int x=int(std::floor(position.x))-centerX,y=int(std::floor(position.y))-centerY;
                                        if(x*x+y*y<=100) {cast(2,UnitTarget{candidate,0,1},false);break;}
                                    }
                                    if(pending) break;
                                }
                            }
                        }
                    }
                }
                stage=4;continue;
            }
            case 4:
                if(has(3)) {const auto chance=roll(100);if(!chance) return {chance.status,{}};if(*chance.value<60 && distance<14) cast(3,*target);}
                stage=5;continue;
            case 5: {
                if(unit->rule.skillIds[1]>0) {
                    const auto chance=roll(100);if(!chance) return {chance.status,{}};
                    if(*chance.value<params[3]) {
                        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
                        if(const auto *room=area.definition.activation.room(unit->position))
                            for(const auto near:area.definition.roomNear.at(size_t(room-area.definition.activation.rooms().data()))) {
                                for(const auto candidate:ports_.areas.roomUnitOrder(near)) if(const auto *other=ports_.monsters.find(candidate)) {
                                    if(candidate==id || other->rule.ai.kind!=MonsterAiKind::Minion || !other->alive() || participants.canHarm(id,candidate) || nativeUniqueMonster(other->identity.rank) || ports_.effects.unitStates(candidate,tick.tick).contains(unit->rule.bloodlustState)) continue;
                                    const auto position=other->position+ports_.areas.at(other->area).definition.origin-area.definition.origin;
                                    const int x=int(std::floor(position.x))-int(std::floor(unit->position.x)),y=int(std::floor(position.y))-int(std::floor(unit->position.y));
                                    if(x*x+y*y<=625) {cast(1,UnitTarget{candidate,0,1});break;}
                                }
                                if(pending) break;
                            }
                        if(!pending) {
                            if(unit->rule.skillIds[4]<=0 || !ports_.monsters.canSpawnNestChild(id,ports_.spatial.nativeCollisionSample())) return wander();
                            const auto selected=area.definition.nativeSummonClasses.find(unit->rule.spawnFallbackClass);
                            if(selected==area.definition.nativeSummonClasses.end()) return {DomainStatus::Unavailable,{}};
                            pendingClass=selected->second;
                            cast(4,*target);
                        }
                    }
                }
                stage=6;continue;
            }
            case 6:
                if(has(3) && distance<14) {cast(3,*target);stage=7;continue;}
                stage=7;continue;
            default: {
                const auto chance=roll(100);if(!chance) return {chance.status,{}};
                if(*chance.value<60) move(point,target->id);
                idle(5);return {DomainStatus::Applied,std::monostate{}};
            }
            }
        }
    });
    return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
}
}
