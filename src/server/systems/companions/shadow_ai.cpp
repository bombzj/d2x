#include "system.hpp"
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
#include "core/random.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <stdexcept>

namespace d2x::server::companions {
struct ShadowDecision {
    enum class Phase { Follow,Idle,OwnerTarget,Basic,Score,Candidates,Fallback,Complete } phase{};
    struct Choice {int skill{};EntityId target;bool gate{};};
    std::vector<Choice> choices;size_t next{},idle{};
    EntityId target;bool combat{},attemptReady{},warrior{},ownerTarget{};
};
namespace {
int squared(Vec first,Vec second) {const int x=int(first.x)-int(second.x),y=int(first.y)-int(second.y);return x*x+y*y;}
struct ShadowLogical {const RoomPopulationLayout *layout{};size_t region{};int index{};};
ShadowLogical shadowLogical(const AreaState &area,Vec point) {
    const auto *room=area.definition.activation.room(point);if(!room) return {};
    const auto at=size_t(room-area.definition.activation.rooms().data());if(at>=area.definition.populationRooms.size()) return {};
    const auto &layout=area.definition.populationRooms[at];const auto global=point+area.definition.origin;
    const int x=int(global.x)/5-layout.x,y=int(global.y)/5-layout.y;
    if(x<0 || y<0 || x>layout.width || y>layout.height || layout.regions.empty()) return {};
    const auto index=layout.single?size_t{}:layout.cells.at(size_t(y)*(layout.width+1)+x);
    return {&layout,index,layout.regions.at(index).index};
}
bool followShadow(const Ports &ports,Companion &pet,const PlayerState &owner,const monsters::Actor &body,EntityId target,bool combat,uint64_t tick) {
    const auto &area=ports.areas.at(body.area);const auto &ownerArea=ports.areas.at(owner.area);
    const auto ownerPoint=owner.position+ownerArea.definition.origin-area.definition.origin;
    const int distance=unitCoordinateDistanceWithSize(int(body.position.x),int(body.position.y),int(ownerPoint.x),int(ownerPoint.y),body.rule.size);
    int count{};for(const auto &[id,entry]:ports.monsters.read().actors) {(void)id;if(entry.owner==owner.player && entry.alive()) ++count;}
    const auto bound=std::min((count>>1)+6,36);
    const bool neutral=!owner.moving && !ports.skills.read().busy(owner.actor,tick);
    if((distance>1 || !neutral || combat) && target && !area.definition.town && distance<=80) return false;
    const auto ownerLogical=shadowLogical(ownerArea,owner.position),unitLogical=shadowLogical(area,body.position);
    int motion=owner.moving?0:2;
    if(ownerLogical.index!=unitLogical.index || distance>bound) motion=1;
    if(distance>50) motion=3;
    const auto lastWarp=owner.lastTeleportPosition-area.definition.origin;
    if(unitCoordinateDistance(int(body.position.x),int(body.position.y),int(lastWarp.x),int(lastWarp.y))<28 &&
       ((motion==1 && distance<30) || motion==2)) motion=4;
    if(distance<=1 && neutral && !combat) motion=5;
    const auto move=[&](Vec point,int velocity=75,int path=13,int maximum=100,bool run=false,EntityId unit={}) {
        monsters::MoveRequest request{body.id,{body.area,area.generation,point},unit,0,velocity,run};
        request.pathType=uint8_t(path);request.pathDistance=uint8_t(maximum);request.pathSteps=1;
        const bool moved=bool(ports.monsters.requestMove(request));if(moved) pet.shadowMoving=true;return moved;
    };
    auto random=body.combatRandom;
    const auto wander=[&](int radius,int velocity=75,int path=13) {const auto point=monsterWanderPoint(ownerPoint,radius,random)-Vec{.5f,.5f};ports.monsters.commitCombatRandom(body.id,random);return move(point,velocity,path);};
    const auto escape=[&](int radius,int path=13) {
        const Vec direction{float((body.position.x>ownerPoint.x)-(body.position.x<ownerPoint.x)),float((body.position.y>ownerPoint.y)-(body.position.y<ownerPoint.y))};
        return move(body.position+direction*float(radius),75,path,radius>5?radius:100);
    };
    if(motion==3) {
        const auto global=owner.position+ownerArea.definition.origin;
        const auto room=ports.areas.roomAtPosition(owner.area,owner.position,int(global.x),int(global.y));
        if(!room) return false;
        const auto *bounds=ownerArea.definition.activation.room(owner.position);if(!bounds) return false;
        int left=int(ownerArea.definition.origin.x)+bounds->x+1,top=int(ownerArea.definition.origin.y)+bounds->y+1;
        int width=bounds->width-1,height=bounds->height-1;
        if(ownerLogical.layout) {
            const auto &box=ownerLogical.layout->regions.at(ownerLogical.region);
            left=box.left*5+1;top=box.top*5+1;width=box.right*5-left;height=box.bottom*5-top;
        }
        auto rule=body.rule;rule.nativeClass=body.assassinPet->definition->warpProbeClass;
        rule.spawnCollision=body.assassinPet->definition->warpProbeCollision;rule.spawnAuxiliary.reset();rule.nativeSpawnFloor=false;rule.enchantment.reset();
        std::optional<Vec> destination;
        // 1.13c 6FC67450 admits probes even with a7=0. The older D2MOO
        // decompilation has the opposite boolean on that branch.
        for(int attempt=0;attempt<20;++attempt) {
            auto loan=ports.areas.roomRandom(*room);if(!loan) return false;
            const int x=left+int(limitedRandom(loan.value->next,uint32_t(std::max(0,width))));
            const int y=top+int(limitedRandom(loan.value->next,uint32_t(std::max(0,height))));
            const Vec requested=Vec{float(x),float(y)}-ownerArea.definition.origin;
            if(ownerLogical.layout && shadowLogical(ownerArea,requested).index!=ownerLogical.index) {
                if(!ports.transactions.consumeRoomRandom(*loan.value)) return false;
                continue;
            }
            monsters::Admission admission;admission.identity.spawnKey="pet-move-probe";admission.area=owner.area;admission.position=requested;admission.rule=rule;admission.tick=tick;
            const auto probe=ports.monsters.prepareRoomSpawn(admission,*loan.value,-1,ownerLogical.layout?std::optional{ownerLogical.region}:std::nullopt,true,ports.spatial.nativeCollisionSample());
            if(!probe || !(*probe.value)->validate()) return false;
            destination=(*probe.value)->position();(*probe.value)->publish();
            if(destination) break;
        }
        if(!destination) return false;
        const auto cell=*destination+ownerArea.definition.origin;
        const auto point=ports.areas.nativeFreeCoordinates(*room,int(cell.x),int(cell.y),body.rule.size,0x1c09,ports.spatial.nativeCollisionSample(0x1c09));
        if(!point || point->room.area!=owner.area) return false;
        const ActorContext context{owner.player,owner.actor,owner.area,ownerArea.generation,0,tick};
        if(!ports.monsters.warpPet(body.id,context,point->position)) return false;
        pet.nextDecision=tick+5;return true;
    }
    if(motion==1) {
        const auto destination=owner.routeDestination+ownerArea.definition.origin-area.definition.origin;
        if(owner.moving && !owner.runningNow && !ownerArea.definition.town && (move(destination) || move((body.position+destination)*.5f))) return true;
        const int velocity=int(rollRandom(random)%40)+40;ports.monsters.commitCombatRandom(body.id,random);
        bool straight{};
        for(auto point=owner.movementHistory.rbegin();point!=owner.movementHistory.rend();++point) {
            if(!point->x || !point->y) continue;
            const auto local=*point-area.definition.origin;
            if(unitCoordinateDistance(int(body.position.x),int(body.position.y),int(local.x),int(local.y))<=5) continue;
            if(move(local,75+velocity) || move(local,75+velocity,15)) return true;
            if(!straight) {straight=true;if(move(local,75+velocity,7)) return true;}
        }
        if(wander(std::max(distance/4,4),90)) return true;
        return false;
    }
    if(motion==2) {
        const bool idle=rollRandom(random)%100>=10;ports.monsters.commitCombatRandom(body.id,random);
        if(idle) {pet.nextDecision=tick+15;return true;}
        const int radius=int(rollRandom(random)%3)+3;ports.monsters.commitCombatRandom(body.id,random);
        if(!wander(radius) && !move(owner.routeDestination+ownerArea.definition.origin-area.definition.origin,75,13,40)) pet.nextDecision=tick+15;
        return true;
    }
    if(motion==4) {
        bool crowded{};
        for(const auto &[id,entry]:ports.monsters.read().actors)
            if(id!=body.id && entry.owner==owner.player && entry.alive() && entry.area==body.area &&
               unitCoordinateDistanceWithSize(int(body.position.x),int(body.position.y),int(entry.position.x),int(entry.position.y),entry.rule.size)<=1) {crowded=true;break;}
        if(!crowded || (!escape(bound) && !wander(bound))) pet.nextDecision=tick+15;
        return true;
    }
    if(motion==5) {
        return wander(bound,75,7) || escape(bound,7) || move(owner.routeDestination+ownerArea.definition.origin-area.definition.origin,75,13,40);
    }
    const auto destination=owner.routeDestination+ownerArea.definition.origin-area.definition.origin;
    const auto heading=nativeDirectionVector(int(destination.x)-int(ownerPoint.x),int(destination.y)-int(ownerPoint.y));
    auto facing=nativeOverlayDirection(heading.index);
    constexpr std::array<int,8> offsets{4,3,2,1,0,7,6,5};
    for(int attempt=0;attempt<7;++attempt,facing=uint8_t((facing+1)&7)) {
        const auto point=destination+monsterDirectionOffset(offsets[facing])*8.f;
        if(shadowLogical(ownerArea,point+area.definition.origin-ownerArea.definition.origin).index==ownerLogical.index &&
           (move(point,75,13,40) || move(Vec{float((int(point.x)+int(body.position.x))/2),float((int(point.y)+int(body.position.y))/2)},75,13,40))) return true;
    }
    const Vec midpoint{float((int(body.position.x)+int(ownerPoint.x))/2),float((int(body.position.y)+int(ownerPoint.y))/2)};
    return wander(4) || move(midpoint,75,13,40) || move(ownerPoint,75,13,40);
}
}
StepStatus System::shadowStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !owner || !body->alive() || !owner->entered || !body->assassinPet) return StepStatus::Complete;
    const auto &program=*body->assassinPet->definition;const auto &catalog=*body->rule.formulas;
    const auto &area=ports_.areas.at(body->area);const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const bool warrior=program.ai==AssassinPetDefinition::Ai::ShadowWarrior;
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
        const auto assigned=ports_.effects.assignUnitSkills(body->id,assignments,tick.tick);if(!assigned) return assigned.status==DomainStatus::Capacity?StepStatus::Blocked:StepStatus::Complete;
        pet.shadowInitialized=true;body=ports_.monsters.find(pet.actor);
    }
    if(!pet.shadowDecision) {
        auto decision=std::make_shared<ShadowDecision>();decision->warrior=warrior;
        const auto target=ai::acquireNativeAiTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},*body,tick,false,false);
        if(target) {decision->target=target->unit.id;decision->combat=unitMeleeReach(area.definition.collision,body->position,body->rule.size,target->position,target->size,body->rule.meleeRange);}
        if(warrior) {
            pet.assassinAi[1]-=1+program.parameters[3][size_t(body->rule.difficulty)];
            if(pet.assassinAi[1]<0 || pet.assassinAi[1]>(std::clamp(program.parameters[7][2],1,256)<<6)) pet.assassinAi[1]=0;
            if((target && target->distance>program.parameters[0][size_t(body->rule.difficulty)]) ||
               unitCoordinateDistance(int(body->position.x),int(body->position.y),int(owner->position.x+ports_.areas.at(owner->area).definition.origin.x-area.definition.origin.x),int(owner->position.y+ports_.areas.at(owner->area).definition.origin.y-area.definition.origin.y))>program.parameters[1][size_t(body->rule.difficulty)]) decision->target={};
        } else if(target && target->distance>program.parameters[1][1]) decision->target={};
        pet.shadowDecision=std::move(decision);
    }
    auto &decision=*pet.shadowDecision;using Phase=ShadowDecision::Phase;
    const auto idle=[&](uint64_t frames) {pet.nextDecision=tick.tick+frames;pet.shadowDecision.reset();return StepStatus::Complete;};
    const auto draw=[&](uint32_t limit) {auto seed=body->combatRandom;const auto value=limitedRandom(seed,limit);ports_.monsters.commitCombatRandom(body->id,seed);return int(value);};
    const auto lowBits=[&](uint32_t mask) {auto seed=body->combatRandom;const auto value=rollRandom(seed)&mask;ports_.monsters.commitCombatRandom(body->id,seed);return int(value);};
    const auto ownStates=[&] {return ports_.effects.unitStates(body->id,tick.tick);};
    const auto charge=[&](int skill) {for(const auto &entry:ports_.effects.progressiveCharges(body->id)) if(entry.skill==skill) return entry.count;return 0;};
    const auto groupActive=[&](int state) {
        if(state<0 || !body->rule.states || !body->rule.states->contains(state)) return false;
        const int group=body->rule.states->at(state).group;if(!group) return false;
        const auto active=ownStates();for(const auto &[id,value]:*body->rule.states) if(id!=state && value.group==group && active.contains(id)) return true;
        return false;
    };
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
            const auto moved=ports_.monsters.requestMove(move);if(moved) pet.shadowMoving=true;return moved.status;
        }
        return ports_.skills.requestCast({body->id,uint16_t(choice.skill),UnitTarget{choice.target,0,uint8_t(victim.player?0:1)},tick.tick,action->second.nativeMode}).status;
    };
    for(;;) {
        if(decision.phase==Phase::Follow) {
            const int ownerDistance=squared(body->position,owner->position+ports_.areas.at(owner->area).definition.origin-area.definition.origin);
            if((warrior || ownerDistance>program.parameters[1][2]*program.parameters[1][2]) && followShadow(ports_,pet,*owner,*body,warrior?decision.target:EntityId{},decision.combat,tick.tick)) {
                pet.shadowDecision.reset();return StepStatus::Complete;
            }
            if(warrior) {
                if(!decision.target) return idle(25);
                const int right=std::max(0,owner->persistent.player.selectedSkills[owner->persistent.player.weaponSet*2+1]);
                const int left=std::max(0,owner->persistent.player.selectedSkills[owner->persistent.player.weaponSet*2]);
                const auto rank=[&](int skill) {const auto source=owner->totals.skillRanks.find(skill);return std::max((source==owner->totals.skillRanks.end()?1:source->second)/2+int(pet.assassinAi[2])/3,1);};
                const std::array<std::pair<int,int>,2> assignments{{{right,rank(right)},{left,rank(left)}}};
                const auto assigned=ports_.effects.assignUnitSkills(body->id,assignments,tick.tick);
                if(!assigned) {
                    if(assigned.status==DomainStatus::Capacity || assigned.status==DomainStatus::Stale || assigned.status==DomainStatus::Conflict) return StepStatus::Blocked;
                    return idle(25);
                }
                body=ports_.monsters.find(pet.actor);
                int selected=lowBits(1)?left:right;
                if(decision.combat && draw(100)<std::clamp(program.parameters[2][size_t(body->rule.difficulty)]-2*std::max(1,int(pet.assassinAi[2])),5,100)) selected=0;
                const auto check=[&](int id) {
                    const auto &row=catalog.skills.at(id);const int ai=row.number("aitype");
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
               followShadow(ports_,pet,*owner,*body,decision.target,decision.combat,tick.tick)) {pet.shadowDecision.reset();return StepStatus::Complete;}
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
                    if(ownerSq<=100) ++closeOwner;if(ownerSq<ownerDistance) {ownerDistance=ownerSq;nearestOwner=id;}
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
                    if(ports_.monsters.requestMove({body->id,{body->area,area.generation,ownerPoint},owner->actor,0,75,true})) {pet.shadowMoving=true;pet.shadowDecision.reset();return StepStatus::Complete;}
                } else {
                    const Vec direction{float((body->position.x>targetPoint.x)-(body->position.x<targetPoint.x)),float((body->position.y>targetPoint.y)-(body->position.y<targetPoint.y))};
                    monsters::MoveRequest escape{body->id,{body->area,area.generation,body->position+direction*8.f},{},0,75,true};
                    escape.pathSteps=1;escape.pathDistance=8;
                    if(ports_.monsters.requestMove(escape)) {pet.shadowMoving=true;pet.shadowDecision.reset();return StepStatus::Complete;}
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
                    if(selfDistance<=25) base-=6;base+=groupActive(row.auraState)?-10:10;skillTarget=body->id;value=base+random();
                } else if(ai==2 && (row.auraState<=0 || !active.contains(row.auraState)) &&
                          (row.auraTargetState<=0 || !ports_.effects.unitStates(target.id(),tick.tick).contains(row.auraTargetState))) {
                    if(selfDistance<=25) base-=10;value=base+random();
                } else if(ai==3) {
                    if(traps>5) base-=2*traps;if(selfDistance<=25) base-=7;if(within32<3) base-=10;
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
                    if(selfDistance<=25) base-=5;if(distance<=25) base-=5;if(progressive) base-=5;
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
                const auto result=use(choice);if(result==DomainStatus::Capacity) return StepStatus::Blocked;
                decision.attemptReady=false;++decision.next;
                if(result==DomainStatus::Applied) {
                    if(warrior) {
                        const auto sampled=ports_.effects.unitStatValues(body->id,tick.tick);auto input=skills::formulaInput(*body,*sampled);SkillFormulaValues values(catalog,input);
                        const auto rank=input.referenceRanks->effective.find(choice.skill);
                        pet.assassinAi[0]=int64_t(tick.tick)+18+values.calculation(FormulaDomain::Skill,choice.skill,rank==input.referenceRanks->effective.end()?1:std::max(1,rank->second),"delay").value_or(0)/3;
                    }
                    pet.nextDecision=std::max(tick.tick+1,body->busyUntil);pet.shadowDecision.reset();return StepStatus::Complete;
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
