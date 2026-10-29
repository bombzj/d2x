#include "system.hpp"
#include "native_pet_move.hpp"
#include "druid_corpse.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/ai/target_query.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/skills/druid_summon.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/units/distance.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x::server::companions {
struct DruidDecision {
    skills::CastRequest request;
    std::array<int64_t,3> after;
    int delay{};
};
StepStatus System::refreshDruid(Companion &pet,TickContext tick) {
    const auto *owner=ports_.players.find(pet.owner);const auto *body=ports_.monsters.find(pet.actor);
    if(!owner || !body || !body->druidPet) return StepStatus::Complete;
    if(owner->rules.skills && !owner->rules.skills->pd2) {
        // Retail caps are synchronous skill/state callbacks in attributes;
        // a later AI poll must not add another final-rank capacity update.
        pet.effectiveRanks=owner->totals.skillRanks;pet.ownerLevel=owner->persistent.player.level;
        return StepStatus::Complete;
    }
    // PlayerPets walks every registered skill instance, including each owned
    // charged entry. Quantities do not change its level; removing an item or
    // switching weapon sets does change the registered source snapshot.
    std::vector<std::pair<NativeSkillSlot,int>> registered;
    for(const auto slot:owner->totals.skillOrder) {
        const auto definition=owner->rules.skills->definitions.find(slot.skill);
        if(definition==owner->rules.skills->definitions.end() || !definition->second.spec.summon ||
           definition->second.spec.summon->petType!=body->rule.petType) continue;
        registered.emplace_back(slot,nativeSkillSlotLevel(slot,owner->totals.skillOrder,owner->totals.skillRanks,
            owner->totals.chargedSkills,owner->rules.skills->formulas->maximumSkillRank));
    }
    if(pet.effectiveRanks==owner->totals.skillRanks && pet.ownerLevel==owner->persistent.player.level &&
       pet.druidSkillRanks==registered) return StepStatus::Complete;
    auto input=skills::formulaInput(*owner);SkillFormulaValues values(*owner->rules.skills->formulas,input);
    int maximum=registered.empty()?body->druidPet->definition->baseMaximum:0;
    for(const auto &[slot,rank]:registered) {
        const auto limit=values.calculation(FormulaDomain::Skill,slot.skill,rank,"petmax");
        if(!limit) throw std::logic_error("Unavailable native druid pet capacity");
        maximum=std::max(maximum,std::max(*limit,1));
    }
    // PlayerPets::sub_6FC7DF40 only changes capacity. Existing passive lists
    // remain the birth snapshot; changing equipment never rerolls summon HP.
    size_t active=0;for(const auto &[id,entry]:state_.companions) {
        const auto *candidate=ports_.monsters.find(id);
        if(entry.owner==pet.owner && !entry.removeAt && candidate && candidate->alive() && candidate->rule.petType==body->rule.petType) ++active;
    }
    for(auto &[id,entry]:state_.companions) {
        if(active<=size_t(maximum)) break;
        const auto *candidate=ports_.monsters.find(id);
        if(entry.owner!=pet.owner || entry.removeAt || !candidate || !candidate->alive() || candidate->rule.petType!=body->rule.petType) continue;
        const auto retired=ports_.monsters.retire(id,tick.tick);if(isDeferred(retired.status)) return StepStatus::Blocked;
        if(!retired) continue;
        entry.removeAt=tick.tick+uint64_t(candidate->rule.deathTicks);
        entry.release=0;
        --active;
    }
    pet.effectiveRanks=owner->totals.skillRanks;pet.ownerLevel=owner->persistent.player.level;
    pet.druidSkillRanks=std::move(registered);return StepStatus::Complete;
}
StepStatus System::druidStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !body->druidPet || !body->alive() || !owner || !owner->entered || owner->persistent.player.hp<=0 ||
       tick.tick<pet.nextDecision || tick.tick<body->busyUntil || ports_.monsters.frozen(body->id,tick.tick)) return StepStatus::Complete;
    const auto resume=[&] {
        auto &decision=*pet.druidDecision;decision.request.tick=tick.tick;
        const auto applied=ports_.skills.requestCast(decision.request);
        if(isDeferred(applied.status)) return StepStatus::Blocked;
        if(applied) {pet.druidAi=decision.after;pet.nextDecision=tick.tick+uint64_t(std::max(decision.delay,1));}
        else pet.nextDecision=tick.tick+1;
        pet.druidDecision.reset();return StepStatus::Complete;
    };
    if(pet.druidDecision) return resume();
    if(!ports_.events.hasCapacity(8,8)) return StepStatus::Blocked;
    if(!body->route.empty()) return StepStatus::Complete;
    if(pet.nativePetMoving) {pet.nativePetMoving=false;pet.nextDecision=tick.tick+uint64_t(std::max(1,body->rule.neutralDelayTicks));return StepStatus::Complete;}
    const auto &program=*body->druidPet->definition;const auto kind=program.ai;
    const auto &area=ports_.areas.at(body->area);const auto &ownerArea=ports_.areas.at(owner->area);
    const Vec ownerPoint=owner->position+ownerArea.definition.origin-area.definition.origin;
    const auto param=[&](size_t index){return program.parameters.at(index).at(size_t(body->rule.difficulty));};
    const auto distance=[&](Vec point,int size=0) {return unitCoordinateDistanceWithSize(int(body->position.x),int(body->position.y),int(point.x),int(point.y),size);};
    const int ownerDistance=unitDistanceWithSizes(int(body->position.x),int(body->position.y),body->rule.size,int(ownerPoint.x),int(ownerPoint.y),2);
    const int velocity=body->rule.nativeVelocity<=0?100:body->petStats.followVelocityBonus;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto point=[&](combat::ParticipantView target) {return target.position()+ports_.areas.at(target.player?target.player->area:target.monster->area).definition.origin-area.definition.origin;};
    const auto idle=[&](int frames) {pet.nextDecision=tick.tick+uint64_t(std::max(frames,1));};
    const auto follow=[&](EntityId target,bool combat,int motion=-1,int speed=0,bool run=false) {
        return followNativePet(ports_,pet,*owner,*body,target,combat,tick.tick,{program.warpProbeClass,program.warpProbeCollision,motion,speed,6,run,kind==DruidPetDefinition::Ai::SpiritWolf || kind==DruidPetDefinition::Ai::DireWolf});
    };
    const auto move=[&](Vec destination,EntityId target={},int path=13,int speed=0,bool run=false,int circle=0) {
        monsters::MoveRequest request{body->id,{body->area,area.generation,destination},target,0,75+speed,run};
        request.pathType=uint8_t(path);request.pathDistance=100;request.pathSteps=1;
        if(path==5 || path==6) {request.circleTurn=circle;request.circleDistance=4;}
        const auto moved=ports_.monsters.requestMove(request);if(moved) pet.nativePetMoving=true;
        return bool(moved);
    };
    const auto escape=[&](Vec target,int radius,bool run=false) {
        const Vec direction{float((body->position.x>target.x)-(body->position.x<target.x)),float((body->position.y>target.y)-(body->position.y<target.y))};
        return move(body->position+direction*float(radius),{},13,0,run);
    };
    const auto chance=[&](int threshold) {
        auto random=ports_.monsters.find(body->id)->combatRandom;const bool selected=int(rollRandom(random)%100)<threshold;
        ports_.monsters.commitCombatRandom(body->id,random);return selected;
    };
    const auto around=[&](int radius) {
        auto random=ports_.monsters.find(body->id)->combatRandom;const auto destination=monsterWanderPoint(body->position,radius,random)-Vec{.5f,.5f};
        ports_.monsters.commitCombatRandom(body->id,random);return move(destination);
    };
    const auto aroundScaled=[&](int radius) {
        if(!ownerDistance) return false;
        return move(Vec{float(int(ownerPoint.x)+radius*(int(body->position.x)-int(ownerPoint.x))/ownerDistance),
            float(int(ownerPoint.y)+radius*(int(body->position.y)-int(ownerPoint.y))/ownerDistance)});
    };
    const auto cast=[&](int skill,EntityId target,int delay,std::array<int64_t,3> after,std::optional<Vec> destination={}) {
        auto decision=std::make_shared<DruidDecision>();decision->request={body->id,uint16_t(skill),UnitTarget{target,0,uint8_t(ports_.players.findActor(target)?0:1)},tick.tick,4,destination};
        if(skill) {
            const auto action=body->rule.skillActions.find(uint16_t(skill));if(action==body->rule.skillActions.end()) {idle(10);return StepStatus::Complete;}
            decision->request.monsterMode=action->second.nativeMode;
        }
        decision->after=after;decision->delay=delay;pet.druidDecision=std::move(decision);return resume();
    };
    const auto melee=[&](combat::ParticipantView target) {return target.id() && unitMeleeReach(area.definition.collision,body->position,body->rule.size,point(target),target.size(),body->rule.meleeRange);};
    if(kind==DruidPetDefinition::Ai::Raven) {
        if(pet.druidAi[0]==-1) {
            pet.druidAi[0]=3;
            if(program.skills[0]>=0) {
                const auto ranks=prepareSkillFormulaRanks(owner->persistent.player.skillRanks,owner->totals.skillRanks,
                    owner->rules.character->innateSkills,owner->totals.chargedSkills,owner->rules.skills->formulas->maximumSkillRank);
                const auto rank=ranks.effective.find(program.skills[0]);const auto &row=body->rule.formulas->skills.at(program.skills[0]);
                pet.druidAi[0]=rank==ranks.effective.end() || rank->second<=0?0:
                    nativeAdd(row.number("param5"),nativeMultiply(rank->second-1,row.number("param6")));
            }
        } else if(!pet.druidAi[0]) {
            const auto retired=ports_.monsters.retire(body->id,tick.tick);if(isDeferred(retired.status)) return StepStatus::Blocked;
            if(retired) pet.removeAt=tick.tick+uint64_t(body->rule.deathTicks);
            return StepStatus::Complete;
        }
        if(ownerDistance>50) {if(!follow({},false,3)) idle(1);return StepStatus::Complete;}
        if(ownerDistance>28) {if(!follow({},false,0,velocity)) idle(1);return StepStatus::Complete;}
        const auto target=ai::acquireNativeAiTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},*body,tick,false,false);
        if(target && pet.druidAi[1]<int64_t(tick.tick) && chance(param(3)) && target->distance<param(4)) {
            const auto victim=participants.find(target->unit.id);
            if(melee(victim)) {
                auto after=pet.druidAi;after[1]=int64_t(tick.tick)+10*param(2);--after[0];
                return cast(0,victim.id(),1,after);
            }
            move(target->position,target->unit.id);return StepStatus::Complete;
        }
        const int average=(param(0)+param(1))/2;
        if(ownerDistance<param(1) || ownerDistance>param(0)) {
            if(!aroundScaled(average) && !follow({},false,1)) idle(1);
            return StepStatus::Complete;
        }
        if(!move(ownerPoint,owner->actor,pet.druidAi[2]?5:6,0,false,pet.druidAi[2]?1:-1)) {
            pet.druidAi[2]=!pet.druidAi[2];
            if(!move(ownerPoint,owner->actor,pet.druidAi[2]?5:6,0,false,pet.druidAi[2]?1:-1) && !aroundScaled(average) && !follow({},false,1)) idle(1);
        }
        return StepStatus::Complete;
    }
    const bool wolf=kind==DruidPetDefinition::Ai::SpiritWolf || kind==DruidPetDefinition::Ai::DireWolf;
    const bool vine=program.vine;
    const int radius=wolf?param(3):kind==DruidPetDefinition::Ai::Bear?28:kind==DruidPetDefinition::Ai::Spirit?24:35;
    auto acquired=ai::acquireNativeAiTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},*body,tick,false,false,radius);
    auto target=acquired?participants.find(acquired->unit.id):combat::ParticipantView{};
    const bool acquiredCombat=melee(target);
    if(wolf || kind==DruidPetDefinition::Ai::Bear) {
        const auto potential=ai::acquireNativeOwnerTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},*owner);
        const auto eligible=[&](combat::ParticipantView candidate) {
            if(!candidate.id()) return false;
            const auto at=point(candidate);
            const int distance=unitCoordinateDistanceWithSize(int(body->position.x),int(body->position.y),int(at.x),int(at.y),body->rule.size);
            const int offset=distance<3?2:distance<11?3:distance<25?4:3;
            const int x=((int(body->position.x)>int(at.x))-(int(body->position.x)<int(at.x)))*offset;
            const int y=((int(body->position.y)>int(at.y))-(int(body->position.y)<int(at.y)))*offset;
            for(const Vec end:std::array{at,at+Vec{float(-y),float(x)},at+Vec{float(y),float(-x)}})
                if(area.definition.collision.collisionSegment(body->position,end,0x45)) return true;
            return false;
        };
        if(target.id() && !eligible(target)) target={};
        if(!target.id() && potential) {
            const auto candidate=participants.find(potential->unit.id);
            const int candidateDistance=kind==DruidPetDefinition::Ai::DireWolf?potential->distance:
                unitCoordinateDistanceWithSize(int(body->position.x),int(body->position.y),int(point(candidate).x),int(point(candidate).y),body->rule.size);
            if(candidateDistance<radius && (kind==DruidPetDefinition::Ai::SpiritWolf?!eligible(candidate):eligible(candidate))) target=candidate;
        }
        if(kind==DruidPetDefinition::Ai::DireWolf && target.id() && ownerDistance>param(3) &&
           unitDistanceWithSizes(int(ownerPoint.x),int(ownerPoint.y),2,int(point(target).x),int(point(target).y),target.size())>param(3)) target={};
    }
    bool combat=acquiredCombat;
    if(wolf || kind==DruidPetDefinition::Ai::Bear) pet.druidAi[2]=int64_t(owner->actor.value);
    if(wolf && area.definition.town) {if(!follow({},false)) idle(33);return StepStatus::Complete;}
    if(vine && ownerDistance>=param(4) && follow({},false,3)) return StepStatus::Complete;
    if(kind==DruidPetDefinition::Ai::PoisonVine) {
        if(area.definition.town) {if(!follow({},false)) idle(param(2));return StepStatus::Complete;}
        if(acquired && acquired->distance>=param(1)) target={};
        if(follow(target.id(),combat)) return StepStatus::Complete;
        if(target.id()) {
            const auto resistance=ports_.effects.unitStatValues(target.id(),tick.tick);
            const bool poisoned=ports_.effects.unitStates(target.id(),tick.tick).contains(body->rule.hitStates.poison.id) ||
                (target.monster?target.monster->poison.has_value():false);
            if(poisoned || (resistance && resistance->get({body->rule.stats->names.at("poisonresist"),0})==100)) {escape(point(target),param(3));return StepStatus::Complete;}
            if(!combat) {move(point(target),target.id(),7);return StepStatus::Complete;}
            if(program.skills[0]>=0 && pet.druidAi[1]+param(0)<int64_t(tick.tick)) {auto after=pet.druidAi;after[1]=int64_t(tick.tick);return cast(program.skills[0],target.id(),1,after);}
        }
        idle(param(2));return StepStatus::Complete;
    }
    if(kind==DruidPetDefinition::Ai::LifeVine || kind==DruidPetDefinition::Ai::ManaVine) {
        int corpseRadius=10;
        const auto stats=ports_.effects.unitStatValues(body->id,tick.tick);
        if(stats && program.skills[0]>0) {
            auto input=skills::formulaInput(*body,*stats);SkillFormulaValues values(*body->rule.formulas,input);
            const auto rank=input.referenceRanks->effective.find(program.skills[0]);
            if(rank!=input.referenceRanks->effective.end()) {
                const auto range=values.calculation(FormulaDomain::Skill,program.skills[0],rank->second,"aurarangecalc");
                if(!range) throw std::logic_error("Unavailable native vine corpse range");
                corpseRadius=std::clamp(*range,5,50);
            }
        }
        auto corpseId=findDruidCorpse(ports_.areas,ports_.players,ports_.monsters,ports_.social,body->id,corpseRadius,tick.tick);
        auto corpse=participants.find(corpseId);if(corpse.id() && distance(point(corpse))>=param(1)) corpse={};
        const bool reachable=melee(corpse);if(follow(corpse.id(),reachable)) return StepStatus::Complete;
        const bool needsResource=kind==DruidPetDefinition::Ai::LifeVine?owner->persistent.player.hp<owner->totals.character.maxLifeFixed:owner->persistent.player.mana<owner->totals.character.maxManaFixed;
        if(corpse.id() && reachable && needsResource && pet.druidAi[1]+param(0)<int64_t(tick.tick)) {
            auto after=pet.druidAi;after[1]=int64_t(tick.tick);return cast(program.skills[0],corpse.id(),1,after);
        }
        if(combat && target.id() && chance(25)) {escape(point(target),param(3));return StepStatus::Complete;}
        if(corpse.id()) move(point(corpse),corpse.id(),7);else idle(param(2));return StepStatus::Complete;
    }
    if(kind==DruidPetDefinition::Ai::Spirit) {
        if(combat && target.id() && chance(param(0)) && escape(point(target),6,true)) return StepStatus::Complete;
        if(chance(param(1))) {target={};combat=false;}
        if(ownerDistance>param(2)) {
            const auto global=owner->position+ownerArea.definition.origin;
            const auto room=ports_.areas.roomAtPosition(owner->area,owner->position,int(global.x),int(global.y));
            if(room) if(const auto free=ports_.areas.nativeFreeCoordinates(*room,int(global.x),int(global.y),body->rule.size,0x1c09,ports_.spatial.nativeCollisionSample(0x1c09))) {
                const ActorContext actor{owner->player,owner->actor,owner->area,ownerArea.generation,0,tick.tick};
                if(ports_.monsters.warpPet(body->id,actor,free->position)) {idle(25);return StepStatus::Complete;}
            }
        }
        if(ownerDistance>param(3) && owner->moving && follow({},false,0,owner->runningNow?60:0)) return StepStatus::Complete;
        if(!follow(target.id(),combat)) idle(25);
        return StepStatus::Complete;
    }
    if(wolf) {
        const int teleport=program.skills[kind==DruidPetDefinition::Ai::DireWolf?1:0];
        if(ownerDistance>50 && teleport>=0) return cast(teleport,{},10,pet.druidAi,ownerPoint);
        if(ownerDistance>param(4) && follow({},false,0,100,true)) return StepStatus::Complete;
        const int followDistance=kind==DruidPetDefinition::Ai::DireWolf?param(3):param(2);
        if(ownerDistance>followDistance && owner->moving && follow({},false,0,owner->runningNow?100:0,owner->runningNow)) return StepStatus::Complete;
        if(follow(target.id(),combat)) return StepStatus::Complete;
        if(kind==DruidPetDefinition::Ai::DireWolf && program.skills[0]>=0 &&
           !ports_.effects.unitStates(body->id,tick.tick).contains(body->rule.formulas->skills.at(program.skills[0]).auraState) &&
           (chance(param(2)) || !target.id() || pet.druidAi[0])) {
            const auto corpseId=findDruidCorpse(ports_.areas,ports_.players,ports_.monsters,ports_.social,body->id,10,tick.tick);
            const auto corpse=participants.find(corpseId);
            if(corpse.id() && distance(point(corpse))<param(3)/2) {
                if(melee(corpse)) {auto after=pet.druidAi;after[0]=0;return cast(program.skills[0],corpse.id(),1,after);}
                if(!combat) {move(point(corpse),corpse.id(),13,velocity,true);pet.druidAi[0]=1;pet.druidAi[1]=int64_t(corpse.id().value);return StepStatus::Complete;}
                return cast(0,target.id(),param(0),pet.druidAi);
            }
        } else pet.druidAi[0]=0;
        if(target.id()) {
            if(combat) return cast(0,target.id(),param(0),pet.druidAi);
            const int leash=unitDistanceWithSizes(int(ownerPoint.x),int(ownerPoint.y),2,int(point(target).x),int(point(target).y),target.size());
            if(kind==DruidPetDefinition::Ai::SpiritWolf && leash>=param(3)) {
                if(ownerDistance<=10) idle(15);
                else move(monsterRadiusApproachPoint(body->position,body->rule.size,ownerPoint,8,6)-Vec{.5f,.5f});
            } else move(point(target),target.id(),13,velocity,true);
        } else if(!chance(param(1)) || !around(10)) idle(15);
        return StepStatus::Complete;
    }
    if(kind==DruidPetDefinition::Ai::Bear) {
        if(ownerDistance>50) {if(!follow({},false,3)) idle(1);return StepStatus::Complete;}
        if(ownerDistance>28) {if(!follow({},false,0,100)) idle(1);return StepStatus::Complete;}
        if(ownerDistance>18 && owner->moving && follow({},false,0,owner->runningNow?100:0)) return StepStatus::Complete;
        if(combat && target.id()) return cast(chance(param(2))?program.skills[0]:0,target.id(),param(0),pet.druidAi);
        if(!target.id()) {if(ownerDistance<=16) idle(15);else if(!follow({},false,0)) idle(1);return StepStatus::Complete;}
        const bool run=chance(param(1));move(point(target),target.id(),13,run?velocity:0);return StepStatus::Complete;
    }
    idle(25);return StepStatus::Complete;
}
}
