#include "system.hpp"
#include "native_area.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/runtime/skill_rules.hpp"
#include <stdexcept>
namespace d2x::server::effects {
DomainResult<> System::nativeArea(const NativeAreaSpec &spec,uint64_t tick,std::shared_ptr<NativeAreaPulse> &retained) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(!retained) retained=std::make_shared<NativeAreaPulse>();
    auto &pulse=*retained;
    if(pulse.complete) return done();
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!pulse.prepared) {
        pulse.spec=spec;pulse.spec.damage.knockback|=(spec.resultFlags&8)!=0;
        pulse.targets=spec.targets?*spec.targets:nativeAuraCandidateRecipients({spec.source,spec.area,spec.radius,spec.filter,false,
            spec.center+ports_.areas.at(spec.area).definition.origin,NativeAuraTargetMode::TargetRange,tick});
        pulse.prepared=true;
    }
    const auto source=participants.find(pulse.spec.source);if(!source.id()) return {DomainStatus::InvalidActor,{}};
    const auto rules=source.player?source.player->rules.skills->stats:source.monster->rule.stats;
    const auto formulas=source.player?source.player->rules.skills->formulas:source.monster->rule.formulas;
    if(!rules || !formulas) return {DomainStatus::Unavailable,{}};
    while(pulse.next<pulse.targets.size()) {
        const auto target=participants.find(pulse.targets[pulse.next]);if(!target.id()) {++pulse.next;continue;}
        if(!pulse.occurrence && pulse.spec.radialBarrier) {
            const auto targetArea=target.player?target.player->area:target.monster->area;
            const auto &area=ports_.areas.at(pulse.spec.area);
            const auto point=target.position()+ports_.areas.at(targetArea).definition.origin-area.definition.origin;
            if(!area.definition.collision.missileSegment(pulse.spec.center,point,{0x805,1})) {++pulse.next;continue;}
        }
        if(!pulse.occurrence) {
            if(nativeAreaOccurrence_>=(uint64_t{1}<<52)-1) return {DomainStatus::Capacity,{}};
            const auto occurrence=(uint64_t{1}<<53)|(nativeAreaOccurrence_+1);
            auto impact=pulse.spec.missileContact?*pulse.spec.missileContact:combat::SpellImpact{};
            impact.source=pulse.spec.source;if(!impact.projectile) impact.projectile=pulse.spec.source;impact.occurrence=occurrence;
            impact.area=target.player?target.player->area:target.monster->area;impact.targets={target.id()};
            impact.nativeAura=impact.synchronous=impact.retainDeadSource=true;
            impact.nativeDamageFlagsOnly=!pulse.spec.rollMissileDamage;
            impact.unblockable=!pulse.spec.missileContact;
            if(!pulse.spec.rollMissileDamage) {
                impact.areaDamage=combat::MissileAreaDamage{};impact.areaDamage->hitFlags=pulse.spec.hitFlags;impact.areaDamage->resultFlags=pulse.spec.resultFlags;
                impact.areaDamage->rolled=std::make_shared<combat::MissileAreaRoll>();impact.areaDamage->rolled->value=pulse.spec.damage;
            }
            if(pulse.spec.directFunction) {
                const auto initial=impact.nativeSkill?impact.nativeSkill->initialDamage:nullptr;
                impact.nativeSkill=std::make_shared<combat::NativeSkillContact>();
                impact.nativeSkill->initialDamage=initial;
                impact.nativeSkill->skill=pulse.spec.skill;impact.nativeSkill->rank=pulse.spec.rank;
                impact.nativeSkill->directFunction=pulse.spec.directFunction;impact.nativeSkill->staticField=pulse.spec.staticField;
                impact.areaDamage->rolled->value.reset();
            }
            if(!impact.missileDamage) {
                const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source.id());
                if(!caster || !caster->programs()) return {DomainStatus::Unavailable,{}};
                const auto &programs=*caster->programs();
                impact.missileDamage=std::make_shared<MissileDamageRollState>();
                // Do107 and other direct area contacts still execute native
                // missile drain. Carry its prepared MPQ metadata as a real
                // missile does; inherited missile contacts retain their data.
                impact.missileDamage->lifeStealDivisor=programs.lifeStealDivisor;
                impact.missileDamage->manaStealDivisor=programs.manaStealDivisor;
                impact.missileDamage->lifeStealOverlay=programs.lifeStealOverlay;
                impact.missileDamage->manaStealOverlay=programs.manaStealOverlay;
            }
            impact.missileDamage->statRules=rules;
            impact.missileDamage->formulas=formulas;
            impact.missileDamage->skill=pulse.spec.skill;impact.missileDamage->level=pulse.spec.rank;
            impact.monsterStates=source.player?MonsterHitStates{source.player->rules.skills->chilled,source.player->rules.skills->poisoned,{}}:source.monster->rule.hitStates;
            impact.missileDamage->uninterruptableState=source.player?source.player->rules.skills->uninterruptable.id:source.monster->rule.uninterruptableState;
            auto queue=ports_.combat.prepareSpells({std::move(impact)});if(!queue) return {queue.status,{}};
            ports_.combat.commitSpells(std::move(*queue.value));pulse.occurrence=occurrence;++nativeAreaOccurrence_;
        }
        const auto projectile=pulse.spec.missileContact?pulse.spec.missileContact->projectile:pulse.spec.source;
        if(ports_.combat.advanceAuraPulse(projectile,*pulse.occurrence,{tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        pulse.occurrence.reset();++pulse.next;
    }
    if(pulse.spec.missileContact) ports_.missiles.finishNativeAreaContact(*pulse.spec.missileContact);
    pulse.complete=true;return done();
}
}
