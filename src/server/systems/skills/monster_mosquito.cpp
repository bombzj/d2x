#include "system.hpp"
#include "runtime.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/skills/rank_bonus.hpp"

namespace d2x::server::skills {
DomainResult<bool> System::releaseMonsterMosquito(MonsterRelease &pending,TickContext tick) {
    const auto done=[] {return DomainResult<bool>{DomainStatus::Applied,true};};
    const auto *source=ports_.monsters.find(pending.request.actor);if(!source) return done();
    if(!pending.mosquito) {
        const auto target=std::get<UnitTarget>(pending.request.target).id;
        const auto point=ports_.monsters.targetPosition(target,source->area);
        if(!point || meleeDistance(source->position,source->rule.size,point->first,point->second)>source->rule.meleeRange) return done();
        const auto sampled=ports_.effects.unitStatValues(source->id,tick.tick);if(!sampled) return {DomainStatus::Unavailable,{}};
        const auto ranks=nativeMonsterSkillRanks(source->rule.baseSkillRanks,*sampled,*source->rule.stats,*source->rule.formulas);
        const auto rank=ranks.find(pending.request.skill);if(rank==ranks.end() || rank->second<1 || rank->second>255) return done();
        pending.mosquito=std::make_shared<effects::NativeAreaPulse>();auto &spec=pending.mosquito->spec;
        spec.source=source->id;spec.area=source->area;spec.skill=pending.request.skill;spec.rank=rank->second;
        spec.targets=std::vector<EntityId>{target};spec.resultFlags=1;spec.directFunction=107;
    }
    pending.nativeCallbackStarted=true;
    const auto result=ports_.effects.nativeArea(pending.mosquito->spec,tick.tick,pending.mosquito);
    if(!result) return {result.status,{}};
    pending.nativeCallbackStarted=false;pending.mosquito.reset();
    if(--pending.mosquitoLoops<=0) return done();
    const auto mode=runtime_->monsterModes.find(source->id);if(mode==runtime_->monsterModes.end()) return done();
    const auto frame=source->rule.formulas->skills.at(pending.request.skill).number("param1");
    mode->second.animation.frame=frame*256+(mode->second.animation.frame&255);
    mode->second.animation.remaining=(mode->second.animation.remaining&~255)+256;
    mode->second.step.ended=false;
    // The next real MonSeq event invokes Do107; no per-frame synthetic hit.
    pending.due=UINT64_MAX;return {DomainStatus::Applied,false};
}
}
