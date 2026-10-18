#include "system.hpp"
#include "runtime.hpp"
#include "evaluation.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x::server::skills {
DomainResult<> System::monsterRunEvent(const FrameEvent &event,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto id=event.owner.unit;
    auto clock=runtime_->monsterModes.find(id);auto release=runtime_->monsters.find(id);
    if(clock==runtime_->monsterModes.end() || release==runtime_->monsters.end()) {cancel({},id);return done();}
    if(clock->second.advanced && clock->second.lastFrame==tick) return done();
    // Retail sequence mode (MonsterMode) tests ENDANIM, advances movement,
    // invokes Do103 on the previous action flag, then calls StopSequence.
    // Ordinary charge's tracked target and one deferred melee do not model it.
    if(clock->second.animation.remaining<=0) {
        const auto ended=ports_.effects.endMode(id,tick);if(!ended) return ended;
        runtime_->monsters.erase(release);state_.interrupt(id,tick);
        ports_.monsters.finishAction(id,tick);ports_.frameEvents.cancel(event.handle);
        runtime_->monsterModes.erase(clock);ports_.monsters.resumeMode(id,tick);return done();
    }
    const auto *source=ports_.monsters.find(id);if(!source) {cancel({},id);return done();}
    auto &pending=release->second;auto &mode=clock->second;
    const auto formulas=source->rule.formulas;const auto &row=formulas->skills.at(pending.request.skill);
    if(!mode.movementAdvanced || mode.movementFrame!=tick) {
        mode.runStopped=pending.hitReleased && !ports_.monsters.advanceDiabloRun(id,tick);
        mode.movementFrame=tick;mode.movementAdvanced=true;
    }
    if(mode.runStopped) {
        pending.hitReleased=false;
        mode.animation.remaining=nativeShiftLeft(row.number("param1"),8);
        mode.animation.frame=nativeShiftLeft(row.number("param2"),8);
    } else if(pending.nativeCallbackStarted || mode.step.event==1 || mode.step.event==2 || mode.step.event==4) {
        const int frame=mode.animation.frame>>8;
        if(frame==row.number("param3")) {
            const auto stats=ports_.effects.unitStatValues(id,tick);if(!stats || !pending.launch) return {DomainStatus::Unavailable,{}};
            auto input=formulaInput(*source,*stats);SkillFormulaValues values(*formulas,input);
            const auto velocity=values.calculation(FormulaDomain::Skill,pending.request.skill,pending.attack.rank,"calc1");
            if(!velocity) return {DomainStatus::Unavailable,{}};
            const auto speed=nativePercentage(std::max(256,nativeShiftLeft(*velocity,8)),
                logicalStatValue(*stats,*source->rule.stats,"velocitypercent"),100);
            const auto started=ports_.monsters.startDiabloRun(id,pending.launch->second,speed);if(!started) return started;
            pending.hitReleased=true;
        } else if(frame==row.number("param4")) {
            mode.animation.remaining=nativeShiftLeft(row.number("param5"),8);
            mode.animation.frame=nativeShiftLeft(row.number("param6"),8);
        } else if(pending.nativeCallbackStarted || mode.step.event==1) {
            if(!pending.directSkill) {
                const auto target=std::get<UnitTarget>(pending.request.target).id;
                const auto point=ports_.monsters.nativeTargetPosition(target,source->area);
                if(point && unitMeleeReach(ports_.areas.at(source->area).definition.collision,source->position,source->rule.size,
                    point->first,point->second,source->rule.meleeRange)) {
                    const auto stats=ports_.effects.unitStatValues(id,tick);if(!stats) return {DomainStatus::Unavailable,{}};
                    const auto ranks=nativeMonsterSkillRanks(source->rule.baseSkillRanks,*stats,*source->rule.stats,*formulas);
                    const auto rank=ranks.find(pending.request.skill);
                    if(rank!=ranks.end() && rank->second>0) {
                        // Current DiabRun declares ResultFlags5: GetResultFlags
                        // is bypassed, so neither accuracy nor block RNG runs.
                        if(!(row.number("resultflags")&1)) return {DomainStatus::NotImplemented,{}};
                        pending.directSkill=std::make_shared<effects::NativeAreaPulse>();auto &spec=pending.directSkill->spec;
                        spec.source=id;spec.area=source->area;spec.skill=pending.request.skill;spec.rank=rank->second;
                        spec.targets=std::vector<EntityId>{target};spec.directFunction=103;
                        spec.resultFlags=uint16_t(row.number("resultflags"));spec.hitFlags=uint32_t(row.number("hitflags"))|0x20;
                    }
                }
            }
            if(pending.directSkill) {
                pending.nativeCallbackStarted=true;
                DomainResult<> result;
                {auto guard=state_.executingRelease(id);result=ports_.effects.nativeArea(pending.directSkill->spec,tick,pending.directSkill);}
                if(!result) return result;
                pending.nativeCallbackStarted=false;pending.directSkill.reset();
            }
            mode.step.event=0;
        }
    }
    source=ports_.monsters.find(id);
    if(!source || !source->alive() || pending.cancelled || source->interruption!=mode.interruption) {cancel({},id);return done();}
    const auto stats=ports_.effects.unitStatValues(id,tick);if(!stats) return {DomainStatus::Unavailable,{}};
    const auto &program=mode.program;
    const int speed=program.useAttackRate?effectiveAttackSpeed(program.animation->speed,
        logicalStatValue(*stats,*source->rule.stats,"item_fasterattackrate"),0,
        nativeSubtract(logicalStatValue(*stats,*source->rule.stats,"attackrate"),100)):program.animation->speed;
    mode.step=advanceMonsterAnimation(mode.animation,program.animation->frames,speed,program.animationStartFrame,true,program.animationFlags);
    mode.lastFrame=tick;mode.advanced=true;ports_.monsters.extendAttack(id,tick+1);return done();
}
}
