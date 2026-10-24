#include "../system.hpp"
#include "../runtime.hpp"
#include "../weapon_melee.hpp"
#include "../native_caster.hpp"
#include "server/systems/combat/system.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/rules/pd2_random.hpp"

namespace d2x::server::skills {
DomainStatus System::activatePd2Bash(Release &pending,const ActorContext &actor) {
    auto &execution=*pending.weaponMelee;
    if(!execution.bashKnockback)
        execution.bashKnockback=execution.contact->hit && execution.contact->lifeHitClassUpdated && pd2CrtRandom(ports_.pd2Random)%100<50;
    if(!*execution.bashKnockback) return DomainStatus::Applied;
    if(!execution.bashOccurrence) {
        const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
        if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
        if(runtime_->nativeKickOccurrence>=(uint64_t{1}<<53)-1) return DomainStatus::Capacity;
        const auto occurrence=(uint64_t{1}<<54)|(runtime_->nativeKickOccurrence+1);
        combat::SpellImpact impact;impact.source=impact.projectile=actor.actor;impact.area=actor.area;
        impact.targets={pending.unit};impact.occurrence=occurrence;impact.synchronous=true;
        impact.weapon=WeaponSkillDamage{};impact.weapon->wearSkill=pending.skill;
        impact.nativeSkill=std::make_shared<combat::NativeSkillContact>();impact.nativeSkill->directFunction=158;
        impact.nativeSkill->skill=pending.skill.sourceId;impact.nativeSkill->rank=pending.skill.rank;
        impact.missileDamage=std::make_shared<MissileDamageRollState>();
        impact.missileDamage->formulas=caster->programs()->formulas;impact.missileDamage->statRules=caster->programs()->stats;
        impact.areaDamage=combat::MissileAreaDamage{};impact.areaDamage->hitFlags=1;impact.areaDamage->resultFlags=8;
        impact.areaDamage->rolled=std::make_shared<combat::MissileAreaRoll>();impact.areaDamage->rolled->value=MissileHitDamage{};
        const auto queued=ports_.combat.enqueue(std::move(impact));if(!queued) return queued.status;
        ++runtime_->nativeKickOccurrence;execution.bashOccurrence=occurrence;
    }
    if(ports_.combat.advanceAuraPulse(actor.actor,*execution.bashOccurrence,{actor.tick})==StepStatus::Blocked) return DomainStatus::Capacity;
    return DomainStatus::Applied;
}
}
