#include "system.hpp"
#include "runtime.hpp"
#include "evaluation.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::skills {
DomainResult<bool> System::releaseMonsterInferno(MonsterRelease &pending,TickContext tick) {
    const auto done=[] {return DomainResult<bool>{DomainStatus::Applied,true};};
    const auto *unit=ports_.monsters.find(pending.request.actor);
    if(!unit || !unit->alive()) return done();
    if(pending.infernoEndAt) return tick.tick>=pending.infernoEndAt?done():DomainResult<bool>{DomainStatus::Applied,false};
    const auto &catalog=*unit->rule.formulas;const int skill=pending.request.skill;
    const auto &row=catalog.skills.at(skill);
    const bool imp=row.number("srvdofunc")==126;
    if((row.number("srvdofunc")!=95 && row.number("srvdofunc")!=152 && !imp) || pending.attack.infernoEndFrames<=0) return {DomainStatus::NotImplemented,{}};
    const auto finish=[&] {
        pending.hitReleased=true;
        pending.infernoEndAt=tick.tick+uint64_t(imp?13:pending.attack.infernoEndFrames);
        if(imp) if(const auto mode=runtime_->monsterModes.find(unit->id);mode!=runtime_->monsterModes.end()) {
            mode->second.frozenSequence=true;mode->second.step={};
        }
        pending.due=pending.infernoEndAt;ports_.monsters.extendAttack(unit->id,pending.due);
        return DomainResult<bool>{DomainStatus::Applied,false};
    };
    const auto target=ports_.monsters.nativeTargetPosition(std::get<UnitTarget>(pending.request.target).id,unit->area);
    if(target) pending.launch=std::pair{unit->position,target->first};
    const auto aim=target?std::optional{target->first}:pending.launch?std::optional{pending.launch->second}:std::nullopt;
    if(!aim) return finish();
    const auto global=*aim+ports_.areas.at(unit->area).definition.origin;
    if(!int(global.x) || !int(global.y)) return finish();
    const auto sampled=ports_.effects.unitStatValues(unit->id,tick.tick);
    if(!sampled) return {DomainStatus::Unavailable,{}};
    const auto ranks=nativeMonsterSkillRanks(unit->rule.baseSkillRanks,*sampled,*unit->rule.stats,catalog);
    const auto found=ranks.find(skill);if(found==ranks.end() || found->second<1 || found->second>255) return finish();
    const int rank=found->second;pending.attack.rank=uint8_t(rank);
    auto context=formulaInput(*unit,*sampled);SkillFormulaValues values(catalog,context);
    const auto calculate=[&](std::string_view field) {
        const auto result=values.calculation(FormulaDomain::Skill,skill,rank,field);
        if(!result) throw std::logic_error("Missing native monster Inferno formula");return *result;
    };
    const auto definition=ports_.missiles.nativeDefinition(row.reference("srvmissilea"));
    if(!definition) return {DomainStatus::Unavailable,{}};
    const auto &missile=catalog.missiles.at(*definition);
    // Retail Do95 leaves the missile skill ID zero. The monster Inferno / Arctic Blast programs
    // use their own missile damage; level controls velocity and beam length.
    missiles::NativeSpawn spawn{unit->id,unit->area,pending.generation,tick.tick,unit->position,*aim,*definition,imp?skill:0,rank};
    if(imp) spawn.postCreationFrames=nativeAdd(missile.number("param2"),calculate("calc2"));
    else {
        int frames=calculate("calc1");if(frames<=0) frames=nativeAdd(missile.number("param2"),rank-1);
        spawn.postCreationFrames=std::clamp(frames,1,255);
    }
    spawn.nativeVelocity=nativeAdd(missile.number("vel"),nativeMultiply(rank,missile.number("vellev"))/8);
    auto prepared=ports_.missiles.prepareNativeBirth(std::span<const missiles::NativeSpawn>{&spawn,1});
    if(!prepared && isDeferred(prepared.status)) return {prepared.status,{}};
    bool created=false;
    if(prepared) {
        // ClientSend controls 0x73 visibility, independently of construction.
        created=bool((*prepared.value)->first());
        const auto committed=ports_.missiles.commitNativeBirth(std::move(*prepared.value));if(!committed) return {committed.status,{}};
    }
    // Do126 still rewinds and schedules after a failed constructor. Do95
    // takes its separate ENDANIM failure branch instead.
    if(!created && !imp) return finish();
    pending.hitReleased=true;
    if(const auto mode=runtime_->monsterModes.find(unit->id);mode!=runtime_->monsterModes.end()) {
        mode->second.animation.frame=pending.attack.infernoFrame*256;
        mode->second.animation.remaining=(pending.attack.animation->frames-pending.attack.infernoFrame)*256;
        mode->second.step.ended=false;
    }
    if(tick.tick>=pending.infernoUntil || !ports_.effects.unitStates(unit->id,tick.tick).contains(catalog.infernoState)) return finish();
    pending.due=tick.tick+uint64_t(imp?3:std::max(1,calculate("calc3")));
    ports_.monsters.extendAttack(unit->id,pending.due);
    return {DomainStatus::Applied,false};
}
}
