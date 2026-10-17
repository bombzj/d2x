#include "system.hpp"
#include "runtime.hpp"
#include "evaluation.hpp"
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
    if(row.number("srvdofunc")!=95 || pending.attack.infernoEndFrames<=0) return {DomainStatus::NotImplemented,{}};
    const auto finish=[&] {
        pending.hitReleased=true;
        pending.infernoEndAt=tick.tick+uint64_t(pending.attack.infernoEndFrames);
        pending.due=pending.infernoEndAt;ports_.monsters.extendAttack(unit->id,pending.due);
        return DomainResult<bool>{DomainStatus::Applied,false};
    };
    const auto target=ports_.monsters.targetPosition(std::get<UnitTarget>(pending.request.target).id,unit->area);
    if(!target) return finish();
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
    // Retail Do95 leaves the missile skill ID zero. FetishInferno damage
    // comes from the missile row; level controls velocity and beam length.
    missiles::NativeSpawn spawn{unit->id,unit->area,pending.generation,tick.tick,unit->position,target->first,*definition,0,rank};
    int frames=calculate("calc1");if(frames<=0) frames=nativeAdd(missile.number("param2"),rank-1);
    spawn.totalFrames=std::clamp(frames,1,255);
    spawn.nativeVelocity=nativeAdd(missile.number("vel"),nativeMultiply(rank,missile.number("vellev"))/8);
    auto prepared=ports_.missiles.prepareNativeBirth(std::span<const missiles::NativeSpawn>{&spawn,1});
    if(!prepared) return isDeferred(prepared.status)?DomainResult<bool>{prepared.status,{}}:finish();
    const bool created=!(*prepared.value)->facts().empty();
    const auto committed=ports_.missiles.commitNativeBirth(std::move(*prepared.value));if(!committed) return {committed.status,{}};
    if(!created) return finish();
    pending.hitReleased=true;
    if(const auto mode=runtime_->monsterModes.find(unit->id);mode!=runtime_->monsterModes.end()) {
        mode->second.animation.frame=pending.attack.infernoFrame*256;
        mode->second.animation.remaining=(pending.attack.animation->frames-pending.attack.infernoFrame)*256;
        mode->second.step.ended=false;
    }
    if(tick.tick>=pending.infernoUntil || !ports_.effects.unitStates(unit->id,tick.tick).contains(catalog.infernoState)) return finish();
    pending.due=tick.tick+uint64_t(std::max(1,calculate("calc3")));
    ports_.monsters.extendAttack(unit->id,pending.due);
    return {DomainStatus::Applied,false};
}
}
