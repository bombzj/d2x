#include "../system.hpp"
#include "../native_caster.hpp"
#include "server/systems/actions/release.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/monsters/pd2/skill_program.hpp"
#include <variant>
namespace d2x::server::skills {
DomainResult<> System::releasePd2MonsterProjectile(MonsterRelease &pending,TickContext tick) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,pending.request.actor);
    if(!caster || !caster->monster || !caster->programs() || !caster->programs()->pd2 ||
        !caster->programs()->definitions.contains(pending.request.skill)) return {DomainStatus::Unavailable,{}};
    const auto &row=caster->monster->rule.formulas->skills.at(pending.request.skill);
    if(!pd2MonsterProjectileProgram(row.number("srvstfunc"),row.number("srvdofunc"))) return {DomainStatus::NotImplemented,{}};
    const auto victim=std::get<UnitTarget>(pending.request.target);
    const auto destination=pending.launch?std::optional{pending.launch->second}:
        [&]()->std::optional<Vec> {
            const auto point=ports_.monsters.nativeTargetPosition(victim.id,pending.area);
            return point?std::optional{point->first}:std::nullopt;
        }();
    if(!destination) return {DomainStatus::InvalidActor,{}};
    const auto actor=caster->context(ports_.areas,tick.tick);
    if(!pending.nativeSkill) {
        auto skill=evaluateNativeSkill(*caster,ports_.effects,pending.request.skill,pending.attack.rank,tick.tick);
        pending.nativeSkill=std::make_shared<Release>(Release{actor,std::move(skill),{},
            {pending.area,pending.generation,*destination},victim.id,tick.tick,victim.type});
    }
    const auto result=activatePd2(*pending.nativeSkill,actor,*destination);
    return {result,result==DomainStatus::Applied?std::optional{std::monostate{}}:std::nullopt};
}
}
