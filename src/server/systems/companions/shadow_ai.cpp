#include "system.hpp"
#include "native_pet_move.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/monsters/native_birth.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/ai/target_query.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/units/distance.hpp"
#include "gameplay/units/direction.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <stdexcept>

namespace d2x::server::companions {
struct ShadowDecision {
    enum class Phase { Sync,Follow,Idle,OwnerTarget,Basic,Score,Candidates,Fallback,Complete } phase{};
    struct Choice {int skill{};EntityId target;bool gate{};};
    std::vector<Choice> choices;size_t next{},idle{};
    EntityId target;bool combat{},attemptReady{},warrior{},ownerTarget{};
};
namespace {
int squared(Vec first,Vec second) {const int x=int(first.x)-int(second.x),y=int(first.y)-int(second.y);return x*x+y*y;}

}
StepStatus System::shadowStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !owner || !body->alive() || !owner->entered || !body->assassinPet) return StepStatus::Complete;
    const auto &program=*body->assassinPet->definition;const auto &catalog=*body->rule.formulas;
    const auto &area=ports_.areas.at(body->area);const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const bool warrior=program.ai==AssassinPetDefinition::Ai::ShadowWarrior;
    const bool pd2Warrior=warrior && owner->rules.skills->pd2;
    const auto point=[&](combat::ParticipantView unit) {return unit.position()+ports_.areas.at(unit.player?unit.player->area:unit.monster->area).definition.origin-area.definition.origin;};
    const auto eligible=[&](int skill) {
        const auto &row=catalog.skills.at(skill);if(row.reference("summon").empty()) return true;
        const auto found=owner->rules.skills->definitions.find(skill);
        if(found==owner->rules.skills->definitions.end() || !found->second.spec.summon) return true;
        return found->second.spec.summon->petType!=body->rule.petType &&
            found->second.spec.summon->monster!=body->identity.monster;
    };
    if(!pet.shadowInitialized) {
        pet.assassinAi={0,0,std::max(1,pet.rank)};
        std::vector<std::pair<int,int>> assignments;
        if(!body->rule.baseSkillRanks.contains(0)) assignments.emplace_back(0,1);
        if(!warrior) for(const auto &[id,row]:catalog.skills) if(row.classId==int(owner->definition.sourceRow) && eligible(id)) {
            const auto found=owner->persistent.player.skillRanks.find(id);const int base=found==owner->persistent.player.skillRanks.end()?1:found->second;
            assignments.emplace_back(id,std::clamp(int(pet.assassinAi[2])/2+base/2,1,24));
        }
        const auto assigned=ports_.effects.assignUnitSkills(body->id,assignments,tick.tick);if(!assigned) return isDeferred(assigned.status)?StepStatus::Blocked:StepStatus::Complete;
        pet.shadowInitialized=true;body=ports_.monsters.find(pet.actor);
    }
    if(!pet.shadowDecision) {
        auto decision=std::make_shared<ShadowDecision>();decision->warrior=warrior;
        decision->phase=pd2Warrior?ShadowDecision::Phase::Sync:ShadowDecision::Phase::Follow;
        const auto target=ai::acquireNativeAiTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},*body,tick,false,false);
        if(target) {decision->target=target->unit.id;decision->combat=unitMeleeReach(area.definition.collision,body->position,body->rule.size,target->position,target->size,body->rule.meleeRange);}
        if(warrior) {
            if(!pd2Warrior) {
                pet.assassinAi[1]-=1+program.parameters[3][size_t(body->rule.difficulty)];
                if(pet.assassinAi[1]<0 || pet.assassinAi[1]>(std::clamp(program.parameters[7][2],1,256)<<6)) pet.assassinAi[1]=0;
            }
            if(!pd2Warrior && ((target && target->distance>program.parameters[0][size_t(body->rule.difficulty)]) ||
               unitCoordinateDistance(int(body->position.x),int(body->position.y),int(owner->position.x+ports_.areas.at(owner->area).definition.origin.x-area.definition.origin.x),int(owner->position.y+ports_.areas.at(owner->area).definition.origin.y-area.definition.origin.y))>program.parameters[1][size_t(body->rule.difficulty)])) decision->target={};
        } else if(target && target->distance>program.parameters[1][1]) decision->target={};
        pet.shadowDecision=std::move(decision);
    }
    auto &decision=*pet.shadowDecision;using Phase=ShadowDecision::Phase;
    const auto idle=[&](uint64_t frames) {pet.nextDecision=tick.tick+frames;pet.shadowDecision.reset();return StepStatus::Complete;};
    const auto draw=[&](uint32_t limit) {auto seed=body->combatRandom;const auto value=limitedRandom(seed,limit);ports_.monsters.commitCombatRandom(body->id,seed);return int(value);};
    const auto lowBits=[&](uint32_t mask) {auto seed=body->combatRandom;const auto value=(pd2Warrior?pd2UnitEventRandom(seed):rollRandom(seed))&mask;ports_.monsters.commitCombatRandom(body->id,seed);return int(value);};
    const auto ownStates=[&] {return ports_.effects.unitStates(body->id,tick.tick);};
    const auto charge=[&](int skill) {for(const auto &entry:ports_.effects.progressiveCharges(body->id)) if(entry.skill==skill) return entry.count;return 0;};
    const auto trapPoolAvailable=[&] {
        // Current 35E990 reads PlayerPets' trap list at 17 * sizeof(PetList),
        // independently of the proposed skill's own pet type.
        constexpr int type=17;const auto &rules=*owner->rules.skills;
        int limit=rules.petTypeBaseLimits.at(type),registeredLimit{};
        auto input=skills::formulaInput(*owner);SkillFormulaValues values(*rules.formulas,input);
        for(const auto &[id,rank]:owner->totals.skillRanks) {
            const auto found=rules.skillPetTypes.find(id);if(found==rules.skillPetTypes.end() || found->second!=type) continue;
            const auto maximum=values.calculation(FormulaDomain::Skill,id,rank,"petmax");
            if(!maximum) throw std::logic_error("Missing native shadow trap capacity");
            registeredLimit=std::max(registeredLimit,std::max(1,*maximum));
        }
        if(registeredLimit) limit=registeredLimit;
        size_t count{};for(const auto &[id,entry]:state_.companions) {
            const auto *candidate=ports_.monsters.find(id);
            if(entry.owner==pet.owner && !entry.removeAt && candidate && candidate->alive() && candidate->rule.petType==type) ++count;
        }
        return count<size_t(std::max(0,limit));
    };
    const auto groupActive=[&](int state) {
        if(state<0 || !body->rule.states || !body->rule.states->contains(state)) return false;
        const int group=body->rule.states->at(state).group;if(!group) return false;
        const auto active=ownStates();for(const auto &[id,value]:*body->rule.states) if(id!=state && value.group==group && active.contains(id)) return true;
        return false;
    };
    const auto recordCooldown=[&](int skill) {
        const auto sampled=ports_.effects.unitStatValues(body->id,tick.tick);
        auto input=skills::formulaInput(*body,*sampled);SkillFormulaValues values(catalog,input);
        const auto rank=input.referenceRanks->effective.find(skill);
        pet.assassinAi[0]=int64_t(tick.tick)+18+values.calculation(FormulaDomain::Skill,skill,
            rank==input.referenceRanks->effective.end()?1:std::max(1,rank->second),"auralencalc").value_or(0)/3;
    };
    bool approaching{},attemptedCast{};
    const auto use=[&](const ShadowDecision::Choice &choice)->DomainStatus {
        if(choice.target==owner->actor || choice.target==body->id) return DomainStatus::Unavailable;
        const auto action=body->rule.skillActions.find(uint16_t(choice.skill));if(action==body->rule.skillActions.end()) return DomainStatus::Unavailable;
        const auto victim=participants.find(choice.target);
        const auto &row=catalog.skills.at(choice.skill);
        const bool combat=victim.id() && unitMeleeReach(area.definition.collision,body->position,body->rule.size,point(victim),victim.size(),body->rule.meleeRange);
        const auto range=row.reference("range");const auto *weapon=body->petWeapon?&*body->petWeapon:nullptr;
        const bool melee=range=="h2h" || (range=="both" && (!weapon || !weapon->ranged));
        if(melee && !combat) {
            if(!victim.id()) return DomainStatus::Unavailable;
            monsters::MoveRequest move{body->id,{body->area,area.generation,point(victim)},victim.id(),body->rule.meleeRange+1,75,true};
            const auto moved=ports_.monsters.requestMove(move);if(moved) {pet.nativePetMoving=true;approaching=true;}return moved.status;
        }
        attemptedCast=true;
        return ports_.skills.requestCast({body->id,uint16_t(choice.skill),UnitTarget{choice.target,0,uint8_t(victim.player?0:1)},tick.tick,action->second.nativeMode}).status;
    };
    for(;;) {
        if(decision.phase==Phase::Sync) {
            const auto ownerStates=ports_.effects.unitStates(owner->actor,tick.tick);
            while(decision.idle<body->nativeSkillOrder.size()) {
                const int skill=body->nativeSkillOrder[decision.idle];const auto &row=catalog.skills.at(skill);
                if(row.number("aitype")!=1 || row.auraState<=0 || !ownerStates.contains(row.auraState) || ownStates().contains(row.auraState)) {++decision.idle;continue;}
                const auto result=use({skill,{},false});if(isDeferred(result)) return StepStatus::Blocked;
                recordCooldown(skill);
                if(result!=DomainStatus::Applied) ports_.monsters.neutralMode(body->id);
                pet.nextDecision=std::max(tick.tick+uint64_t(result==DomainStatus::Applied?1:10),body->busyUntil);pet.shadowDecision.reset();return StepStatus::Complete;
            }
            if(uint16_t(program.parameters[7][2])<=1) return idle(100);
            pet.assassinAi[1]-=1+program.parameters[3][size_t(body->rule.difficulty)];
            if(pet.assassinAi[1]<0 || pet.assassinAi[1]>int64_t(std::min(uint16_t(program.parameters[7][2]),uint16_t(256)))*64) pet.assassinAi[1]=0;
            decision.phase=Phase::Follow;continue;
        }
        if(decision.phase==Phase::Follow) {
            if(pd2Warrior) {
                const auto followed=followPd2Shadow(ports_,pet,*owner,*body,decision.target,tick.tick,{program.warpProbeClass,program.warpProbeCollision});
                if(isDeferred(followed.status)) return StepStatus::Blocked;
                if(followed && *followed.value) {pet.shadowDecision.reset();return StepStatus::Complete;}
            }
            const int ownerDistance=squared(body->position,owner->position+ports_.areas.at(owner->area).definition.origin-area.definition.origin);
            if(!pd2Warrior && (warrior || ownerDistance>program.parameters[1][2]*program.parameters[1][2]) && followNativePet(ports_,pet,*owner,*body,warrior?decision.target:EntityId{},decision.combat,tick.tick,{program.warpProbeClass,program.warpProbeCollision})) {
                pet.shadowDecision.reset();return StepStatus::Complete;
            }
            if(warrior) {
                if(!pd2Warrior && !decision.target) return idle(25);
                const int right=std::max(0,owner->persistent.player.selectedSkills[owner->persistent.player.weaponSet*2+1]);
                const int left=std::max(0,owner->persistent.player.selectedSkills[owner->persistent.player.weaponSet*2]);
                const auto rank=[&](int skill) {const auto source=owner->totals.skillRanks.find(skill);return std::max((source==owner->totals.skillRanks.end()?1:source->second)/2+int(pet.assassinAi[2])/3,1);};
                if(!pd2Warrior) {
                    const std::array<std::pair<int,int>,2> assignments{{{right,rank(right)},{left,rank(left)}}};
                    const auto assigned=ports_.effects.assignUnitSkills(body->id,assignments,tick.tick);
                    if(!assigned) {
                        if(isDeferred(assigned.status) || assigned.status==DomainStatus::Stale || assigned.status==DomainStatus::Conflict) return StepStatus::Blocked;
                        return idle(25);
                    }
                    body=ports_.monsters.find(pet.actor);
                }
                const bool hasLeft=!pd2Warrior || body->rule.baseSkillRanks.contains(left);
                const bool hasRight=!pd2Warrior || body->rule.baseSkillRanks.contains(right);
                if(!hasLeft && !hasRight) return idle(25);
                int selected=hasLeft && hasRight?(lowBits(1)?left:right):hasLeft?left:right;
                if(!pd2Warrior && decision.combat && draw(100)<std::clamp(program.parameters[2][size_t(body->rule.difficulty)]-2*std::max(1,int(pet.assassinAi[2])),5,100)) selected=0;
                const auto check=[&](int id) {
                    if(pd2Warrior && !body->rule.baseSkillRanks.contains(id)) return false;
                    const auto &row=catalog.skills.at(id);const int ai=row.number("aitype");
                    if(pd2Warrior) {
                        if(row.classId!=int(owner->definition.sourceRow) || !eligible(id)) return false;
                        const auto states=ownStates();bool qualified{};
                        switch(ai) {
                        case 1: qualified=row.auraState>0 && !states.contains(row.auraState);break;
                        case 2: qualified=bool(decision.target) && (row.auraState<=0 || !states.contains(row.auraState)) &&
                            (row.auraTargetState<=0 || !ports_.effects.unitStates(decision.target,tick.tick).contains(row.auraTargetState));break;
                        case 3: qualified=bool(decision.target) && trapPoolAvailable();break;
                        case 5: case 11: case 12: qualified=bool(decision.target);break;
                        case 4: case 13: qualified=bool(decision.target) && decision.combat;break;
                        default: break;
                        }
                        if(!qualified || (id && uint32_t(pet.assassinAi[0])>uint32_t(tick.tick))) return false;
                        if(!id) return true;
                        auto seed=body->combatRandom;const auto value=pd2UnitEventRandom(seed)%100;
                        ports_.monsters.commitCombatRandom(body->id,seed);return value>=25;
                    }
                    if(row.classId!=int(owner->definition.sourceRow) || !eligible(id) || (decision.combat?(ai!=4 && ai!=13):(ai==4 || ai==13))) return false;
                    const auto states=ownStates();
                    if(ai==1 && row.auraState>0 && states.contains(row.auraState)) return false;
                    if((ai==2 && row.auraState>0 && states.contains(row.auraState)) || (row.auraTargetState>0 && ports_.effects.unitStates(decision.target,tick.tick).contains(row.auraTargetState))) return false;
                    if(row.number("progressive") && charge(id)>=3) return false;
                    if(id) {
                        const auto sampled=ports_.effects.unitStatValues(body->id,tick.tick);auto input=skills::formulaInput(*body,*sampled);
                        const auto rank=input.referenceRanks->effective.at(id);
                        SkillFormulaValues values(catalog,input);const auto cost=values.parameter(FormulaDomain::Skill,id,rank,"mana");
                        if(!cost) throw std::logic_error("Missing native shadow mana-cost formula");
                        const int mana=std::max(0,*cost);
                        if(draw(100)>100-160*mana/100 || pet.assassinAi[0]>int64_t(tick.tick)) return false;
                        if(pet.assassinAi[1]<std::clamp(program.parameters[7][1],1,128) || pet.assassinAi[1]>32*std::clamp(program.parameters[7][2],1,256)) pet.assassinAi[1]=std::clamp(program.parameters[7][1],1,128);
                        const int first=draw(uint32_t(pet.assassinAi[1])),second=draw(100);if(first>second) return false;
                        pet.assassinAi[1]+=mana*(320-pet.assassinAi[2])/(pet.assassinAi[2]+100);
                    }
                    return true;
                };
                if(!check(selected)) {selected=selected==left?right:left;if(!check(selected)) selected=0;}
                decision.choices={{selected,decision.target,false}};decision.phase=Phase::Candidates;continue;
            }
            decision.phase=decision.target?Phase::OwnerTarget:Phase::Idle;continue;
        }
        if(decision.phase==Phase::Idle) {
            while(decision.idle<body->nativeSkillOrder.size()) {
                const int skill=body->nativeSkillOrder[decision.idle++];const auto &row=catalog.skills.at(skill);
                if(row.number("aitype")!=1 || row.auraState<=0 || ownStates().contains(row.auraState)) continue;
                if(groupActive(row.auraState) && draw(100)>=4) continue;
                if(draw(100)>=60) continue;
                decision.choices={{skill,{},false}};decision.next=0;decision.phase=Phase::Candidates;break;
            }
            if(decision.phase==Phase::Idle) decision.phase=Phase::OwnerTarget;
            continue;
        }
        if(decision.phase==Phase::OwnerTarget) {
            const auto cast=ports_.skills.read().casts.find(owner->actor);
            if(cast!=ports_.skills.read().casts.end()) {
                const auto target=participants.find(cast->second.target);
                if(target.id() && target.damageable() && participants.canHarm(body->id,target.id())) {decision.target=target.id();decision.ownerTarget=true;}
            }
            if(squared(body->position,owner->position+ports_.areas.at(owner->area).definition.origin-area.definition.origin)>=144 &&
               followNativePet(ports_,pet,*owner,*body,decision.target,decision.combat,tick.tick,{program.warpProbeClass,program.warpProbeCollision})) {pet.shadowDecision.reset();return StepStatus::Complete;}
            if(!decision.target) return idle(25);
            decision.phase=Phase::Basic;continue;
        }
        if(decision.phase==Phase::Basic) {
            if(!decision.attemptReady) {
                if(!decision.combat || draw(100)>=std::clamp(program.parameters[2][size_t(body->rule.difficulty)]-2*std::max(1,int(pet.assassinAi[2])),5,100)) {decision.phase=Phase::Score;continue;}
                decision.attemptReady=true;
            }
            const auto result=use({0,decision.target,false});if(result==DomainStatus::Capacity) return StepStatus::Blocked;
            decision.attemptReady=false;if(result==DomainStatus::Applied) {pet.nextDecision=body->busyUntil;pet.shadowDecision.reset();return StepStatus::Complete;}
            decision.phase=Phase::Score;continue;
        }
        if(decision.phase==Phase::Score) {
            EntityId nearestSelf,nearestOwner,lastSpecial;int selfDistance=INT_MAX,ownerDistance=INT_MAX,closeSelf{},closeOwner{},within32{},traps{};
            if(const auto *room=area.definition.activation.room(body->position)) {
                const auto index=size_t(room-area.definition.activation.rooms().data());
                for(const auto near:area.definition.roomNear.at(index)) for(const auto id:ports_.areas.roomUnitOrder(near)) {
                    const auto unit=participants.find(id);if(!unit.id() || id==body->id || (unit.player?unit.player->persistent.player.hp<=0:!unit.monster->alive())) continue;
                    if(unit.monster && participants.auraAligned({nullptr,body},unit) && unit.monster->assassinPet && !unit.monster->assassinPet->definition->warp) {++traps;continue;}
                    if(!unit.damageable() || !participants.canHarm(body->id,id)) continue;
                    const auto at=point(unit);const int ownerSq=squared(owner->position+ports_.areas.at(owner->area).definition.origin-area.definition.origin,at);
                    if(ownerSq<=100) ++closeOwner;
                    if(ownerSq<ownerDistance) {ownerDistance=ownerSq;
                    nearestOwner=id;
                    }
                    const int selfSq=squared(body->position,at);if(selfSq>1024) continue;
                    ++within32;if(selfSq<=100) ++closeSelf;if(selfSq<selfDistance) {selfDistance=selfSq;nearestSelf=id;}
                    if(unit.player || unit.monster->rule.boss || unit.monster->rule.primeEvil || nativeUniqueMonster(unit.monster->identity.rank)) lastSpecial=id;
                }
            }
            if(!decision.combat && !decision.ownerTarget) {
                if(nearestOwner) decision.target=nearestOwner;
                else if(lastSpecial && squared(body->position,point(participants.find(lastSpecial)))<1024) decision.target=lastSpecial;
                const auto current=participants.find(decision.target);
                if(current.monster && !current.monster->rule.boss && !current.monster->rule.primeEvil && !nativeUniqueMonster(current.monster->identity.rank)) {
                    const auto master=participants.find(ports_.monsters.minionOwner(current.id()));
                    if(master.id() && master.aliveIn(body->area) && squared(body->position,point(master))<1024) decision.target=master.id();
                }
            }
            const auto target=participants.find(decision.target);if(!target.id()) return idle(15);
            const auto targetPoint=point(target);const int distance=squared(body->position,targetPoint);
            if(closeSelf>3 && lowBits(31)<2*closeSelf) {
                const auto ownerPoint=owner->position+ports_.areas.at(owner->area).definition.origin-area.definition.origin;
                if(squared(body->position,ownerPoint)>36) {
                    if(ports_.monsters.requestMove({body->id,{body->area,area.generation,ownerPoint},owner->actor,0,75,true})) {pet.nativePetMoving=true;pet.shadowDecision.reset();return StepStatus::Complete;}
                } else {
                    const Vec direction{float((body->position.x>targetPoint.x)-(body->position.x<targetPoint.x)),float((body->position.y>targetPoint.y)-(body->position.y<targetPoint.y))};
                    monsters::MoveRequest escape{body->id,{body->area,area.generation,body->position+direction*8.f},{},0,75,true};
                    escape.pathSteps=1;escape.pathDistance=8;
                    if(ports_.monsters.requestMove(escape)) {pet.nativePetMoving=true;pet.shadowDecision.reset();return StepStatus::Complete;}
                }
            }
            const int life=body->maximumLife?int(body->life*100/body->maximumLife):0;
            const auto active=ownStates();const bool progressive=program.parameters[0][2]>0 && !ports_.effects.progressiveCharges(body->id).empty();
            const auto resistances=ports_.effects.unitStatValues(target.id(),tick.tick);if(!resistances) return idle(15);
            const auto resistance=[&](const SkillFormulaRecord &row) {
                const std::string_view name=row.elementType==0?"damageresist":row.elementType==1?"fireresist":row.elementType==2?"lightresist":
                    row.elementType==3?"magicresist":row.elementType==4 || row.elementType==12?"coldresist":row.elementType==5?"poisonresist":"";
                return name.empty()?0:resistances->get({body->rule.stats->names.at(std::string(name)),0});
            };
            auto sampled=ports_.effects.unitStatValues(body->id,tick.tick);auto input=skills::formulaInput(*body,*sampled);
            int chargeBonus{};decision.choices={{0,decision.target,true}};
            for(const int id:body->nativeSkillOrder) {
                const auto rank=input.referenceRanks->effective.find(id);if(rank==input.referenceRanks->effective.end() || rank->second<=0) continue;
                const auto &row=catalog.skills.at(id);int value{},base=resistance(row)/-10+row.number("reqlevel")/4+rank->second+row.number("aibonus");
                EntityId skillTarget=decision.target;const int ai=row.number("aitype"),count=charge(id);
                const auto random=[&] {return draw(uint32_t(std::max(0,program.parameters[1][0])));};
                if(ai==1 && (row.auraState<=0 || active.contains(row.auraState))) {
                    if(selfDistance<=25) base-=6;
                    base+=groupActive(row.auraState)?-10:10;
                    skillTarget=body->id;
                    value=base+random();
                } else if(ai==2 && (row.auraState<=0 || !active.contains(row.auraState)) &&
                          (row.auraTargetState<=0 || !ports_.effects.unitStates(target.id(),tick.tick).contains(row.auraTargetState))) {
                    if(selfDistance<=25) base-=10;
                    value=base+random();
                } else if(ai==3) {
                    if(traps>5) base-=2*traps;
                    if(selfDistance<=25) base-=7;
                    if(within32<3) base-=10;
                    value=base+random()+3*(within32-3);
                } else if(ai==4 || ai==12) {
                    if(ai==12 && target.monster && target.monster->rule.drain<25) continue;
                    if(distance>program.parameters[0][0]*program.parameters[0][0]) base-=10;
                    base+=program.parameters[0][1];if(decision.combat || distance<=25) base+=10;
                    if(row.number("progressive")) chargeBonus+=count;
                    if(ai==12) {if(count<3) value=base+random()+(life<75?8:0)+(life<50?12:0);}
                    else if(!row.number("progressive")) value=base+(program.parameters[0][2]<=0 || progressive?4*chargeBonus+3:-10)+random();
                    else if(count<3) value=program.parameters[0][2]+base+random();
                } else if(ai==5 || ai==11) {
                    if(!area.definition.collision.collisionSegment(body->position,targetPoint,4)) continue;
                    if(row.reference("srvmissile").empty()) {
                        const auto missile=catalog.missileIds.find(row.reference("srvmissilea"));
                        if(missile!=catalog.missileIds.end()) {const int range=catalog.missiles.at(missile->second).number("range")-1;if(distance>=range*range) continue;}
                    }
                    if(selfDistance<=25) base-=5;
                    if(distance<=25) base-=5;
                    if(progressive) base-=5;
                    value=base+random()+(ai==11?3*within32:0);
                } else if(ai==13) {
                    base+=program.parameters[0][1];base+=program.parameters[0][2]<=0 || progressive?chargeBonus:-5;
                    if((life<50 || closeSelf>3) && nearestOwner && closeOwner<4 && squared(body->position,point(participants.find(nearestOwner)))>25) {skillTarget=nearestOwner;value=base+20+random();}
                    else if(distance>=25) value=base+(distance>324?10:0)+random();
                }
                if(value>0 && decision.choices.size()<48) decision.choices.push_back({id,skillTarget,true});
            }
            std::reverse(decision.choices.begin(),decision.choices.end());decision.next=0;decision.phase=Phase::Candidates;continue;
        }
        if(decision.phase==Phase::Candidates) {
            while(decision.next<decision.choices.size()) {
                const auto choice=decision.choices[decision.next];
                if(!decision.attemptReady) {
                    if(choice.gate && !lowBits(3)) {++decision.next;continue;}
                    decision.attemptReady=true;
                }
                const auto result=use(choice);if(isDeferred(result)) return StepStatus::Blocked;
                decision.attemptReady=false;++decision.next;
                if(result==DomainStatus::Applied) {
                    if(warrior && !approaching) recordCooldown(choice.skill);
                    pet.nextDecision=std::max(tick.tick+1,body->busyUntil);pet.shadowDecision.reset();return StepStatus::Complete;
                }
                if(pd2Warrior) {
                    if(attemptedCast) {recordCooldown(choice.skill);ports_.monsters.neutralMode(body->id);}
                    pet.nextDecision=tick.tick+uint64_t(attemptedCast?10:1);
                    pet.shadowDecision.reset();return StepStatus::Complete;
                }
            }
            if(warrior) return idle(25);
            if(!decision.target) {decision.phase=Phase::Idle;decision.choices.clear();decision.next=0;continue;}
            decision.phase=Phase::Fallback;continue;
        }
        if(decision.phase==Phase::Fallback) {
            const auto result=use({0,decision.target,false});if(result==DomainStatus::Capacity) return StepStatus::Blocked;
            if(result==DomainStatus::Applied) {pet.nextDecision=std::max(tick.tick+1,body->busyUntil);pet.shadowDecision.reset();return StepStatus::Complete;}
            return idle(15);
        }
        return idle(15);
    }
}
}
