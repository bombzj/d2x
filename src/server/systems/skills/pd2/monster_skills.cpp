#include "../system.hpp"
#include "../native_caster.hpp"
#include "../runtime.hpp"
#include "server/systems/actions/release.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "server/systems/combat/requests.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/actions/volley.hpp"
#include "gameplay/actions/timeline.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <stdexcept>
#include "gameplay/monsters/pd2/skill_program.hpp"
#include <variant>
namespace d2x::server::skills {
DomainResult<> System::releasePd2PoisonEgg(MonsterRelease &pending,TickContext tick) {
    // Core 17d5… 3B1090: Do22(original skill/rank), then Do84(159,rank).
    // The native wrapper ignores Do22's return. Host capacity retries must
    // finish its publication first, without replaying it during hatching.
    if(!pending.pd2EggNovaReleased) {
        const auto nova=releaseMonsterNova(pending,tick);
        if(isDeferred(nova.status)) return nova;
        pending.pd2EggNovaReleased=true;pending.formulaPrepared=false;
    }
    const auto result=releaseMonsterHatch(pending,tick,159);
    if(isDeferred(result.status)) pending.nativeCallbackStarted=true;
    return result;
}
DomainResult<std::shared_ptr<actions::SkillRelease>> System::startPd2HirelingStrafe(const CastRequest &request,const MonsterAttackRule &attack,int speed) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,request.actor);
    if(!caster || !caster->monster || !caster->monster->hireling || !caster->monster->petWeapon || !caster->programs() ||
        !attack.animation || !caster->programs()->definitions.contains(request.skill)) return {DomainStatus::Unavailable,{}};
    auto input=caster->formulaInput(ports_.effects,request.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
    const auto formula=[&](std::string_view field) {
        const auto value=values.calculation(FormulaDomain::Skill,request.skill,attack.rank,field);
        if(!value) throw std::logic_error("Missing PD2 hireling Strafe formula");
        return *value;
    };
    const auto targets=ports_.effects.nativeAuraCandidateRecipients({caster->id(),caster->area(),formula("aurarangecalc"),0xa783u,false,
        {},effects::NativeAuraTargetMode::OwnerRange,request.tick});
    const auto count=strafeShotCount(int(targets.size()),formula("calc1"),formula("calc3"));
    const auto unit=std::get<UnitTarget>(request.target);
    const auto position=ports_.monsters.nativeTargetPosition(unit.id,caster->area());
    if(!position || count<=0) return {DomainStatus::Unavailable,{}};
    auto skill=evaluateNativeSkill(*caster,ports_.effects,request.skill,attack.rank,request.tick);
    auto release=std::make_shared<Release>(Release{caster->context(ports_.areas,request.tick),std::move(skill),{},
        {caster->area(),ports_.areas.at(caster->area()).generation,position->first},unit.id,request.tick,unit.type});
    release->weapon=caster->monster->petWeapon;release->rewindRemaining=count;release->animationAnchor=int64_t(request.tick);
    release->weaponRollback=caster->programs()->formulas->skills.at(request.skill).number("param6");
    release->weaponFrames=attack.animation->frames;release->weaponSpeed=speed;release->animationEvents=attack.animationFlags;
    return {DomainStatus::Applied,std::move(release)};
}
DomainStatus System::pd2MonsterStaticField(Release &pending,const ActorContext &actor) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->monster || !caster->programs()) return DomainStatus::InvalidActor;
    const auto &rules=*caster->programs();
    const auto center=caster->position();
    if(!pending.directSkill) {
        const auto expected=caster->random();auto random=expected;
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        auto input=caster->formulaInput(ports_.effects,actor.tick,draw);SkillFormulaValues values(*rules.formulas,input);
        const auto formula=[&](std::string_view field) {
            const auto value=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,field);
            if(!value) throw std::logic_error("Missing PD2 monster Static Field formula");
            return *value;
        };
        NativeStaticField program;program.minimumDamage=formula("calc2");program.percent=formula("calc1");
        program.minimumPercent=rules.staticMinimum.at(size_t(caster->monster->rule.difficulty));
        const auto length=values.parameter(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"edln");
        if(!length) return DomainStatus::Unavailable;
        program.length=*length;const auto &row=rules.formulas->skills.at(pending.skill.sourceId);
        program.element=MissileDamageElement(row.elementType);
        auto pulse=std::make_shared<effects::NativeAreaPulse>();auto &spec=pulse->spec;
        spec.source=actor.actor;spec.area=actor.area;spec.center=caster->position();spec.skill=pending.skill.sourceId;spec.rank=pending.skill.rank;
        spec.directFunction=20;spec.staticField=program;
        spec.targets=ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,formula("aurarangecalc"),uint32_t(row.number("aurafilter")),false,
            {},effects::NativeAuraTargetMode::OwnerRange,actor.tick});
        const auto consumed=ports_.monsters.consumeRandom(actor.actor,expected,random);if(!consumed) return consumed.status;
        pending.directSkill=std::move(pulse);
    }
    pending.nativeCallbackStarted=true;
    auto result=ports_.effects.nativeArea(pending.directSkill->spec,actor.tick,pending.directSkill);
    if(result && pending.skill.curse)
        result=ports_.effects.monsterCurse(actor.actor,pending.skill.sourceId,pending.skill.rank,center,actor.tick,pending.monsterCurse);
    if(result) pending.nativeCallbackStarted=false;
    return result.status;
}
DomainResult<> System::releasePd2HirelingSkill(MonsterRelease &pending,TickContext tick) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,pending.request.actor);
    if(!caster || !caster->monster || !caster->monster->hireling || !caster->programs() ||
        !caster->programs()->definitions.contains(pending.request.skill)) return {DomainStatus::Unavailable,{}};
    const auto unit=std::get<UnitTarget>(pending.request.target);
    const auto point=ports_.monsters.nativeTargetPosition(unit.id,pending.area);
    const auto target=point?point->first:caster->position();
    auto actor=caster->context(ports_.areas,tick.tick);
    if(!pending.nativeSkill) {
        auto skill=evaluateNativeSkill(*caster,ports_.effects,pending.request.skill,pending.attack.rank,tick.tick);
        pending.nativeSkill=std::make_shared<Release>(Release{actor,std::move(skill),{},
            {pending.area,pending.generation,target},unit.id,tick.tick,unit.type});
        if(pending.nativeSkill->skill.weapon) pending.nativeSkill->weapon=caster->monster->petWeapon;
    }
    auto &release=*pending.nativeSkill;
    const auto result=release.skill.effect==SkillBehavior::StaticField?pd2MonsterStaticField(release,actor):activatePd2(release,actor,target);
    if(result==DomainStatus::Applied && release.rewindRemaining>0 && --release.rewindRemaining>0) {
        const auto current=nativeCaster(ports_.players,ports_.monsters,pending.request.actor);
        if(!current) return {DomainStatus::InvalidActor,{}};
        auto input=current->formulaInput(ports_.effects,tick.tick);SkillFormulaValues values(*current->programs()->formulas,input);
        const auto radius=values.calculation(FormulaDomain::Skill,pending.request.skill,pending.attack.rank,"aurarangecalc");
        if(!radius) throw std::logic_error("Missing PD2 hireling Strafe range");
        const auto candidates=ports_.effects.nativeAuraCandidateRecipients({current->id(),current->area(),*radius,0xa783u,false,
            {},effects::NativeAuraTargetMode::OwnerRange,tick.tick});
        const auto next=nativeSkillSuccessor(candidates,uint32_t(release.unit.value));
        if(next) {
            release.unit=next;release.unitType=ports_.players.findActor(next)?0:1;
            pending.request.target=UnitTarget{next,0,release.unitType};
            const auto elapsed=int((int64_t(tick.tick)-release.animationAnchor)*(100-release.weaponRollback)/100);
            auto &mode=runtime_->monsterModes.at(pending.request.actor);
            mode.pd2Rewind=rewoundPlayerActionTimeline(release.weaponFrames,release.weaponSpeed,elapsed,release.animationEvents);
            mode.pd2RewindStarted=tick.tick;mode.pd2RewindCursor=0;
            release.animationAnchor=int64_t(tick.tick)-elapsed;
        } else release.rewindRemaining=0;
    }
    pending.nativeCallbackStarted=release.nativeCallbackStarted;
    return {result,result==DomainStatus::Applied?std::optional{std::monostate{}}:std::nullopt};
}
DomainResult<> System::releasePd2MonsterSkill(MonsterRelease &pending,TickContext tick) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,pending.request.actor);
    if(!caster || !caster->monster || !caster->programs() || !caster->programs()->pd2 ||
        !caster->programs()->definitions.contains(pending.request.skill)) return {DomainStatus::Unavailable,{}};
    const auto &row=caster->monster->rule.formulas->skills.at(pending.request.skill);
    const bool joust=pending.attack.action==MonsterAttackRule::Action::Pd2Joust &&
        !row.number("srvstfunc") && row.number("srvdofunc")==162;
    const bool teleport=pending.attack.action==MonsterAttackRule::Action::Teleport &&
        !row.number("srvstfunc") && row.number("srvdofunc")==155;
    if(!joust && !teleport && !pd2MonsterProjectileProgram(row.number("srvstfunc"),row.number("srvdofunc"))) return {DomainStatus::NotImplemented,{}};
    const auto victim=std::get<UnitTarget>(pending.request.target);
    const auto destination=row.number("srvdofunc")==143?std::optional{caster->position()}:
        pending.request.position?pending.request.position:
        pending.launch?std::optional{pending.launch->second}:
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
        if(joust) {
            pending.nativeSkill->nativeContact=std::make_shared<combat::NativeSkillContact>();
            pending.nativeSkill->nativeContact->monsterProgram=pending.attack;
        }
    }
    const auto result=teleport?activatePd2MonsterTeleport(*pending.nativeSkill,actor,*destination):activatePd2(*pending.nativeSkill,actor,*destination);
    pending.nativeCallbackStarted=pending.nativeSkill->nativeCallbackStarted;
    return {result,result==DomainStatus::Applied?std::optional{std::monostate{}}:std::nullopt};
}
}
