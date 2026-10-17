#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/objects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "gameplay/monsters/ranged_decision.hpp"
#include "gameplay/monsters/special_decision.hpp"
#include "gameplay/monsters/act2_decision.hpp"
#include "gameplay/monsters/boss_decision.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/monsters/curse_decision.hpp"
#include "server/systems/effects/system.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x::server::ai {
DomainResult<> System::perform(Controller &controller,std::function<DomainResult<>()> operation) {
    if(!controller.randomPublished) {
        const auto consumed=ports_.monsters.consumeRandom(controller.actor,controller.randomExpected,controller.random);
        if(!consumed) return consumed;
        controller.randomPublished=true;
    }
    if(controller.pending.empty()) {
        const auto result=operation();
        if(!isDeferred(result.status)) return result;
    }
    controller.pending.push_back(std::move(operation));
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::requestAction(Controller &controller,const actions::MonsterCastRequest &request) {
    return perform(controller,[this,request]{return ports_.skills.requestCast(request);});
}
std::optional<System::ShootableTarget> System::acquireTarget(const monsters::Actor &source,TickContext tick,bool missileRay,bool requireSight) const {
    return acquireNativeAiTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},source,tick,missileRay,requireSight);
}
std::optional<System::ShootableTarget> System::shootableTarget(const monsters::Actor &source,TickContext tick) const {
    return acquireTarget(source,tick,true,true);
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    std::erase_if(state_.controllers, [&](const auto &entry) {
        const auto *actor=ports_.monsters.find(entry.first);return !actor || actor->life<=0;
    });
    return StepStatus::Complete;
}
StepStatus System::think(EntityId id,TickContext tick) {
    const auto *actor=ports_.monsters.find(id);if(!actor) return StepStatus::Complete;
    const auto &monster=*actor;
    if(const auto previous=state_.controllers.find(id);previous!=state_.controllers.end() && previous->second.admissionRevision!=monster.admissionRevision)
        state_.controllers.erase(previous);
    if(const auto found=state_.controllers.find(id);found!=state_.controllers.end() && !found->second.pending.empty()) {
        auto &controller=found->second;
        while(!controller.pending.empty()) {
            const auto result=controller.pending.front()();
            if(isDeferred(result.status)) return StepStatus::Blocked;
            controller.pending.pop_front();
            if(!result) {controller.pending.clear();break;}
        }
        ports_.monsters.scheduleAi(id,controller.nextDecision);return StepStatus::Complete;
    }
    bool blocked=false, sampled=false;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    // One owner invocation. Early exits finish this native AI callback.
    do {
        if(monster.questNpc || monster.rule.trap!=MonsterRule::Trap::None) continue;
        // MPQ Idle / old monster_ai::PrisonDoor: a destructible gate never
        // pursues or attacks, including its permitted content substitute.
        if(monster.implementation==MonsterKind::PrisonDoor) {ports_.monsters.stop(id);continue;}
        if (monster.life <= 0 || (monster.owner && !monster.conversion && !monster.revived())) continue;
        auto [entry, fresh] = state_.controllers.try_emplace(id);
        auto &controller = entry->second;
        if (fresh) {
            controller.admissionRevision=monster.admissionRevision;
            controller.random = monster.combatRandom;
            if(monster.rule.ai.kind==MonsterAiKind::Sarcophagus) controller.skillDeadline=monster.admissionTick+uint64_t(monster.rule.ai.params[0]);
            if(monster.rule.ai.kind==MonsterAiKind::FoulCrowNest) controller.lastSpawn=monster.admissionTick;
        }
        if(controller.observedAiFunctionRevision!=monster.aiFunctionRevision) {
            // AITHINK_ExecuteAiFn clears the three family parameters and AI
            // commands on an actual function switch; selector-only Confuse
            // and Attract do not reset the family controller.
            const auto function=monster.curseControl.ai==CurseAi::DimVision || monster.curseControl.ai==CurseAi::Terror || monster.curseControl.ai==CurseAi::Taunt?monster.curseControl.ai:CurseAi::None;
            const bool hasAlternative=monster.rule.ai.kind==MonsterAiKind::SandMaggot || monster.rule.ai.kind==MonsterAiKind::BatDemon;
            const bool defer=!controller.alternateAi && controller.function==CurseAi::None && function!=CurseAi::None && hasAlternative;
            controller.observedAiFunctionRevision=monster.aiFunctionRevision;controller.function=function;controller.alternateAi=defer;
            if(!defer) {
                controller.curseParameters.fill(0);controller.phase=controller.loop=0;controller.commanded=false;controller.charged=false;
                controller.pursuing=false;controller.target.reset();controller.nextPath=0;
            }
            controller.nextDecision=std::max(controller.nextDecision,tick.tick);
        }
        if(controller.observedAiThinkRevision!=monster.aiThinkRevision) {
            controller.observedAiThinkRevision=monster.aiThinkRevision;controller.nextDecision=monster.aiThinkAt;
        }
        controller.nextDecision=ports_.monsters.resumeAi(id,tick.tick,controller.nextDecision);
        if (monster.busyUntil > tick.tick || ports_.monsters.frozen(id,tick.tick) || monster.stunnedUntil>tick.tick || ports_.skills.busy(id,tick.tick) || controller.nextDecision > tick.tick) continue;
        controller.actor = id; controller.nextDecision = tick.tick + uint64_t(monster.rule.decisionTicks);
        controller.random=controller.randomExpected=monster.combatRandom;controller.randomPublished=false;sampled=true;
        const auto &area=ports_.areas.at(monster.area);
        const bool active = ports_.areas.active(monster.area, monster.position);
        if(!active || (area.definition.town && monster.rule.ai.kind!=MonsterAiKind::GoodNpcRanged)) {controller.pursuing=false;ports_.monsters.stop(id);continue;}
        // AiUtil::sub_6FCF2110 and DRLGROOM_CheckLOSDraw: initial acquisition
        // in rooms without LOSDraw requires a clear missile-barrier ray.
        // Native flag 8 keeps pursuit enabled once a target was acquired.
        const auto *room=area.definition.activation.room(monster.position);
        const bool requireSight=room && !room->checkLosDraw && !controller.acquiredTarget;
        if(controller.alternateAi) {
            if(monster.rule.ai.kind==MonsterAiKind::SandMaggot && monster.burrowed && monster.rule.skillIds[0]>=0) {
                const auto skill=uint16_t(monster.rule.skillIds[0]);const auto definition=monster.rule.skillActions.find(skill);
                if(definition!=monster.rule.skillActions.end()) {
                    const auto cast=requestAction(controller,{id,skill,PointTarget{monster.area,area.generation,monster.position},tick.tick,definition->second.nativeMode});
                    if(isDeferred(cast.status)) {blocked=true;controller.nextDecision=tick.tick+1;continue;}
                    if(cast) {controller.nextDecision=tick.tick+30;continue;}
                }
            }
            if(monster.rule.ai.kind==MonsterAiKind::BatDemon) {
                if(monster.aiPose==11) {controller.phase=1;controller.nextDecision=tick.tick+1;continue;}
                if(controller.phase==4) {
                    controller.phase=1;const auto landed=perform(controller,[this,id,tick]{return ports_.monsters.aiPose(id,10,false,false,tick.tick);});
                    if(isDeferred(landed.status)) {blocked=true;controller.phase=4;}
                    controller.nextDecision=tick.tick+1;continue;
                }
            }
            // The alternate callback re-enters AITHINK_ExecuteAiFn on the next
            // think, after returning a burrowed/roosting family to a usable mode.
            controller.alternateAi=false;controller.curseParameters.fill(0);controller.phase=controller.loop=0;
            controller.commanded=controller.charged=controller.pursuing=false;controller.target.reset();controller.nextPath=0;
            controller.nextDecision=tick.tick+1;continue;
        }
        const bool blind=monster.curseControl.ai==CurseAi::DimVision;
        if(monster.curseControl.ai==CurseAi::Taunt) {
            const auto caster=participants.find(monster.tauntTarget?monster.tauntTarget:monster.curseControl.caster);
            const auto casterRegion=caster.player?caster.player->area:caster.monster?caster.monster->area:monster.area;
            if(!caster.id() || ports_.areas.at(casterRegion).definition.town ||
               ports_.areas.at(casterRegion).definition.act!=area.definition.act) {
                ports_.monsters.curseControl(id,{});ports_.monsters.stop(id);controller.target.reset();controller.nextDecision=tick.tick+1;continue;
            }
            const auto point=caster.position()+ports_.areas.at(casterRegion).definition.origin-area.definition.origin;
            const auto attack=[&](UnitTarget victim) {
                const auto cast=requestAction(controller,{id,0,victim,tick.tick,4});
                if(isDeferred(cast.status)) {blocked=true;controller.nextDecision=tick.tick+1;}
            };
            const bool directCombat=meleeDistance(monster.position,monster.rule.size,point,caster.size())<=monster.rule.meleeRange;
            const auto alternative=controller.curseParameters[0] && !directCombat?acquireTarget(monster,tick,false,requireSight):std::optional<ShootableTarget>{};
            const bool alternativeCombat=alternative && meleeDistance(monster.position,monster.rule.size,alternative->position,alternative->size)<=monster.rule.meleeRange;
            const auto decision=decideTaunt(true,false,controller.curseParameters[0],directCombat,alternativeCombat);
            if(decision==CurseDecisionAction::Attack) {
                attack(alternativeCombat?alternative->unit:UnitTarget{caster.id(),0,uint8_t(caster.monster?1:0)});continue;
            }
            controller.target=caster.id();controller.acquiredTarget=true;
            const auto moved=ports_.monsters.requestMove({id,{monster.area,area.generation,point},caster.id(),0,75,false,0,0,0,13,7});
            if(!moved) {
                auto random=monster.combatRandom;
                if(rollRandom(random)%100>=70) {ports_.monsters.stop(id);controller.nextDecision=tick.tick+10;}
                else {const auto destination=monsterWanderPoint(monster.position,4,random);ports_.monsters.requestMove({id,{monster.area,area.generation,destination},{},0,75,false});}
                const auto committed=ports_.monsters.consumeRandom(id,monster.combatRandom,random);
                if(committed) {controller.random=random;controller.randomPublished=true;}
                if(!committed) {blocked=true;controller.nextDecision=tick.tick+1;}
            }
            continue;
        }
        if(monster.curseControl.ai==CurseAi::Terror) {
            const auto victim=acquireTarget(monster,tick,false,requireSight);
            const auto origin=monster.curseControl.origin.value_or(victim?victim->position:monster.position);
            const int distance=monster.curseControl.origin?monsterAiDistance(origin,0,monster.position):victim?victim->distance:INT32_MAX;
            const bool activeState=monster.curseControl.state<0 || ports_.effects.unitStates(id,tick.tick).contains(monster.curseControl.state);
            const bool combat=!monster.curseControl.origin && victim && meleeDistance(monster.position,monster.rule.size,victim->position,victim->size)<=monster.rule.meleeRange;
            const auto decision=decideFear({controller.curseParameters,distance,monster.rule.nativeVelocity,monster.rule.nativeRunVelocity,
                activeState,combat,monster.rule.curseAiInteract,monster.rule.attacks.contains(4),monster.rule.curseAiRunMode});
            if(decision.action==CurseDecisionAction::Restore) {
                ports_.monsters.curseControl(id,{});ports_.monsters.stop(id);controller.nextDecision=tick.tick+1;continue;
            }
            if(decision.action==CurseDecisionAction::Idle) {ports_.monsters.stop(id);controller.nextDecision=tick.tick+uint64_t(decision.idleFrames);continue;}
            if(decision.action==CurseDecisionAction::Attack) {
                const auto cast=requestAction(controller,{id,0,victim->unit,tick.tick,4});
                if(isDeferred(cast.status)) {blocked=true;controller.nextDecision=tick.tick+1;}continue;
            }
            controller.curseParameters=decision.parameters;
            if(decision.firstEscape) {auto control=monster.curseControl;control.target={};ports_.monsters.curseControl(id,std::move(control));}
            const auto destination=monsterRetreatPoint(monster.position,origin,30);
            const auto moved=ports_.monsters.requestMove({id,{monster.area,area.generation,destination},{},0,75+decision.velocityBonus,decision.run});
            if(!moved && decision.fallback) {
                auto random=monster.combatRandom;const auto fallback=monsterWanderPoint(monster.position,6,random);
                if(ports_.monsters.consumeRandom(id,monster.combatRandom,random)) {
                    controller.random=random;controller.randomPublished=true;
                    ports_.monsters.requestMove({id,{monster.area,area.generation,fallback},{},0,75,false});
                }
            }
            controller.target.reset();controller.pursuing=bool(moved);controller.nextDecision=tick.tick+1;continue;
        }
        if(monster.rule.ai.kind==MonsterAiKind::GoodNpcRanged) {
            if(monster.moving || !monster.route.empty() || monster.aiPose>1) {
                ports_.monsters.stop(id);controller.nextDecision=tick.tick+5;continue;
            }
            const auto target=area.definition.town?std::optional<ShootableTarget>{}:shootableTarget(monster,tick);
            controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;
            if(target && target->distance<20) {
                if(rollRandom(controller.random)%100<30) {
                    const auto cast=requestAction(controller,{id,0,target->unit,tick.tick,4});
                    if(isDeferred(cast.status)) {blocked=true;controller.nextDecision=tick.tick+1;}
                } else if(rollRandom(controller.random)%100<30) {
                    const int turn=(rollRandom(controller.random)&255)<128?1:-1;
                    const auto moved=perform(controller,[this,id,target,tick,turn] {
                        const auto *unit=ports_.monsters.find(id);if(!unit) return DomainResult<>{DomainStatus::InvalidActor,{}};
                        return ports_.monsters.requestMove({id,{unit->area,ports_.areas.at(unit->area).generation,target->position},target->unit.id,0,75,false,0,turn,4});
                    });
                    controller.pursuing=bool(moved);controller.nextDecision=tick.tick+1;
                } else {ports_.monsters.stop(id);controller.nextDecision=tick.tick+10;}
            } else if(rollRandom(controller.random)%100<20) {
                const auto point=monsterWanderPoint(monster.position,5,controller.random);
                const auto moved=perform(controller,[this,id,point] {
                    const auto *unit=ports_.monsters.find(id);if(!unit) return DomainResult<>{DomainStatus::InvalidActor,{}};
                    return ports_.monsters.requestMove({id,{unit->area,ports_.areas.at(unit->area).generation,point},{},0,75,false});
                });
                controller.pursuing=bool(moved);controller.nextDecision=tick.tick+1;
            } else {ports_.monsters.stop(id);controller.nextDecision=tick.tick+10;}
            continue;
        }
        const auto acquired=acquireTarget(monster,tick,false,requireSight);
        std::optional<UnitTarget> target;Vec targetPosition;int targetSize=2,distance=INT32_MAX;
        if(acquired) {target=acquired->unit;targetPosition=acquired->position;targetSize=acquired->size;distance=acquired->distance;}
        if (controller.target != (target ? std::optional(target->id) : std::nullopt)) {
            controller.pursuing = false;
            if(monster.rule.ai.kind!=MonsterAiKind::SandRaider) controller.charged = false;
            ports_.monsters.stop(id);
        }
        controller.target = target ? std::optional(target->id) : std::nullopt;
        if(blind) {
            const bool combat=target && meleeDistance(monster.position,monster.rule.size,targetPosition,targetSize)<=monster.rule.meleeRange;
            auto random=monster.combatRandom;
            const auto percentage=combat && !monster.rule.curseAiInteract && monster.rule.attacks.contains(4)?0:rollRandom(random)%100;
            const auto decision=decideDimVision(combat,monster.rule.curseAiInteract,monster.rule.attacks.contains(4),percentage);
            if(decision==CurseDecisionAction::Attack) {
                const auto result=requestAction(controller,{id,0,*target,tick.tick,4});
                if(isDeferred(result.status)) {blocked=true;controller.nextDecision=tick.tick+1;}
            } else {
                if(decision==CurseDecisionAction::Wander) {
                    const auto destination=monsterWanderPoint(monster.position,3,random);
                    ports_.monsters.requestMove({id,{monster.area,area.generation,destination},{},0,75,false});
                } else {ports_.monsters.stop(id);controller.nextDecision=tick.tick+10;}
                if(!ports_.monsters.consumeRandom(id,monster.combatRandom,random)) {blocked=true;continue;}
                controller.random=random;controller.randomPublished=true;
            }
            continue;
        }
        if (!target) {
            controller.pursuing = false; ports_.monsters.stop(id);
            const auto kind=monster.rule.ai.kind;
            if(kind==MonsterAiKind::MaggotEgg || kind==MonsterAiKind::SandMaggotQueen || kind==MonsterAiKind::SandMaggot || kind==MonsterAiKind::BatDemon || kind==MonsterAiKind::Vulture)
                blocked|=act2Action(id,{id,0,1},monster.position,monster.rule.size,tick,controller)==StepStatus::Blocked;
            continue;
        }
        controller.acquiredTarget=true;
        if(monster.rule.opensDoors && ports_.objects.openMonsterDoor(id,targetPosition,tick.tick)) {
            ports_.monsters.stop(id);controller.pursuing=false;controller.nextDecision=tick.tick+5;continue;
        }
        if(const auto unique=uniqueAction(id,*target,targetPosition,tick,controller)) {blocked|=*unique==StepStatus::Blocked;continue;}
        const auto &rules = monster.rule.ai;
        if(hasAct2Decision(rules.kind)) {
            blocked|=act2Action(id,*target,targetPosition,targetSize,tick,controller)==StepStatus::Blocked;
            continue;
        }
        if(rules.kind==MonsterAiKind::FoulCrowNest) {
            if(nestFamily(id,*target,distance,tick,controller)==StepStatus::Blocked) blocked=true;
            continue;
        }
        if (hasMeleeDecision(rules.kind) || hasRangedDecision(rules.kind) || hasSpecialDecision(rules.kind) || rules.kind==MonsterAiKind::FallenShaman || hasBossDecision(rules.kind)) {
            if (familyAction(id,*target,targetPosition,targetSize,tick,controller) == StepStatus::Blocked) blocked = true;
            continue;
        }
    } while(false);
    if(blocked) return StepStatus::Blocked;
    if(const auto current=state_.controllers.find(id);current!=state_.controllers.end()) {
        auto &controller=current->second;
        const auto *live=ports_.monsters.find(id);
        if(!live || !live->alive()) {controller.pending.clear();ports_.monsters.scheduleAi(id,0);return StepStatus::Complete;}
        // A reaction/neutral wait can return before a new decision is sampled.
        // Damage may have advanced the unit seed since the previous decision;
        // that old loan must not be recommitted by this wake-up event.
        if(sampled && !controller.randomPublished && controller.actor) {
            const auto consumed=ports_.monsters.consumeRandom(id,controller.randomExpected,controller.random);
            if(!consumed) return StepStatus::Blocked;
            controller.randomPublished=true;
        }
        if(!controller.pending.empty()) return StepStatus::Blocked;
        ports_.monsters.scheduleAi(id,std::max({controller.nextDecision,live->busyUntil,live->frozenUntil,live->stunnedUntil,live->knockedUntil}));
    }
    return StepStatus::Complete;
}
}
