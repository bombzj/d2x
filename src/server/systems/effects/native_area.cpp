#include "system.hpp"
#include "native_area.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include <stdexcept>
namespace d2x::server::effects {
DomainResult<> System::nativeArea(const NativeAreaSpec &spec,uint64_t tick,std::shared_ptr<NativeAreaPulse> &retained) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(!retained) retained=std::make_shared<NativeAreaPulse>();auto &pulse=*retained;if(pulse.complete) return done();
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!pulse.prepared) {
        pulse.spec=spec;pulse.spec.damage.knockback|=(spec.resultFlags&8)!=0;
        pulse.targets=nativeAuraCandidateRecipients({spec.source,spec.area,spec.radius,spec.filter,false,
            spec.center+ports_.areas.at(spec.area).definition.origin,NativeAuraTargetMode::TargetRange});
        pulse.prepared=true;
    }
    const auto source=participants.find(pulse.spec.source);if(!source.id()) return {DomainStatus::InvalidActor,{}};
    const auto rules=source.player?source.player->rules.skills->stats:source.monster->rule.stats;
    const auto formulas=source.player?source.player->rules.skills->formulas:source.monster->rule.formulas;
    if(!rules || !formulas) return {DomainStatus::Unavailable,{}};
    while(pulse.next<pulse.targets.size()) {
        const auto target=participants.find(pulse.targets[pulse.next]);if(!target.id()) {++pulse.next;continue;}
        if(!pulse.occurrence) {
            if(nativeAreaOccurrence_>=(uint64_t{1}<<52)-1) return {DomainStatus::Capacity,{}};
            const auto occurrence=(uint64_t{1}<<53)|(nativeAreaOccurrence_+1);
            combat::SpellImpact impact;impact.source=impact.projectile=pulse.spec.source;impact.occurrence=occurrence;
            impact.area=target.player?target.player->area:target.monster->area;impact.targets={target.id()};
            impact.nativeAura=impact.synchronous=impact.nativeDamageFlagsOnly=impact.unblockable=impact.retainDeadSource=true;
            impact.areaDamage=combat::MissileAreaDamage{};impact.areaDamage->hitFlags=pulse.spec.hitFlags;impact.areaDamage->resultFlags=pulse.spec.resultFlags;
            impact.areaDamage->rolled=std::make_shared<combat::MissileAreaRoll>();impact.areaDamage->rolled->value=pulse.spec.damage;
            impact.missileDamage=std::make_shared<MissileDamageRollState>();impact.missileDamage->statRules=rules;impact.missileDamage->formulas=formulas;
            impact.missileDamage->skill=pulse.spec.skill;impact.missileDamage->level=pulse.spec.rank;
            impact.monsterStates=source.player?MonsterHitStates{source.player->rules.skills->chilled,source.player->rules.skills->poisoned,{}}:source.monster->rule.hitStates;
            impact.missileDamage->uninterruptableState=source.player?source.player->rules.skills->uninterruptable.id:source.monster->rule.uninterruptableState;
            auto queue=ports_.combat.prepareSpells({std::move(impact)});if(!queue) return {queue.status,{}};
            ports_.combat.commitSpells(std::move(*queue.value));pulse.occurrence=occurrence;++nativeAreaOccurrence_;
        }
        if(ports_.combat.advanceAuraPulse(pulse.spec.source,*pulse.occurrence,{tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        pulse.occurrence.reset();++pulse.next;
    }
    pulse.complete=true;return done();
}
}
