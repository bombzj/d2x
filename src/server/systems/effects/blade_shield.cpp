#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <stdexcept>

namespace d2x::server::effects {
struct BladeShieldPulse {
    ActorContext actor;SkillCastSpec skill;
    std::shared_ptr<const MissileHitDamage> base;
    std::vector<EntityId> targets;size_t next{};
    std::optional<uint64_t> occurrence;
    bool prepared{},complete{};
};
DomainResult<> System::advanceBladeShield(BladeShieldPulse &pulse,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(pulse.complete) return done();
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,pulse.actor.actor);
    if(!caster || !caster->programs() || (caster->player && !caster->player->entered)) {pulse.complete=true;return done();}
    const auto &rules=*caster->programs();const auto &area=ports_.areas.at(caster->area());
    if(!pulse.prepared) {
        if((caster->player?caster->player->persistent.player.hp<=0:!caster->monster->alive()) || area.definition.town || !pulse.skill.assassin) {pulse.complete=true;return done();}
        const auto *states=unitEffects(caster->id());
        if(!states || !states->hasState(pulse.skill.assassin->state.id,tick)) {pulse.complete=true;return done();}
        const auto expected=caster->random();auto random=expected;
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        auto input=caster->formulaInput(*this,tick,draw);SkillFormulaValues values(*rules.formulas,input);
        auto radius=values.calculation(FormulaDomain::Skill,pulse.skill.sourceId,pulse.skill.rank,"prgcalc1");
        if(!radius) throw std::logic_error("Unavailable native Blade Shield progressive range");
        if(!*radius) radius=values.calculation(FormulaDomain::Skill,pulse.skill.sourceId,pulse.skill.rank,"aurarangecalc");
        if(!radius) throw std::logic_error("Unavailable native Blade Shield range");
        auto base=std::make_shared<const MissileHitDamage>(rollNativeSkillDamage(values,pulse.skill.sourceId,pulse.skill.rank,random));
        const auto &native=rules.formulas->skills.at(pulse.skill.sourceId);
        auto targets=nativeAuraCandidateRecipients({caster->id(),caster->area(),*radius,uint32_t(native.number("aurafilter")),false});
        const auto rating=values.parameter(FormulaDomain::Skill,pulse.skill.sourceId,pulse.skill.rank,"toht");
        if(!rating) throw std::logic_error("Unavailable native Blade Shield to-hit factor");
        const auto actor=caster->context(ports_.areas,tick);
        const auto consumed=caster->monster?ports_.monsters.consumeRandom(caster->id(),expected,random):ports_.transactions.consumeRandom(actor,expected,random);
        if(!consumed) return consumed;
        pulse.skill.weapon.emplace();pulse.skill.weapon->attackRating=*rating;pulse.skill.weapon->mode="a1";pulse.skill.weapon->noAmmo=true;
        pulse.base=std::move(base);pulse.targets=std::move(targets);pulse.prepared=true;
    }
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    while(pulse.next<pulse.targets.size()) {
        if(!pulse.occurrence) {
            const auto target=participants.find(pulse.targets[pulse.next]);
            if(!target.id()) {++pulse.next;continue;}
            if(bladeShieldOccurrence_>=(uint64_t{1}<<56)-1) return {DomainStatus::Capacity,{}};
            const auto occurrence=(uint64_t{1}<<57)|(bladeShieldOccurrence_+1);
            const auto actor=caster->context(ports_.areas,tick);
            missiles::Spawn request{actor,pulse.skill,{},target.position(),true};
            request.deathTrigger=true;request.directArea=target.player?target.player->area:target.monster->area;
            request.nativeMeleeBase=pulse.base;request.nativeMeleeOccurrence=occurrence;
            WeaponSkillDamage weapon;weapon.weapon=caster->player?caster->player->totals.equipment.weapons[0]:caster->monster->petWeapon.value_or(WeaponDamage{});
            weapon.level=caster->level();request.weapon=std::move(weapon);
            const auto queued=ports_.missiles.directContact(request,{target.id()});if(!queued) return {queued.status,{}};
            ++bladeShieldOccurrence_;pulse.occurrence=occurrence;
        }
        if(ports_.combat.advanceAuraPulse(caster->id(),*pulse.occurrence,{tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        pulse.occurrence.reset();++pulse.next;
    }
    pulse.complete=true;return done();
}
DomainResult<> System::bladeShield(const ActorContext &actor,const SkillCastSpec &skill,std::shared_ptr<BladeShieldPulse> &pulse) {
    if(!pulse) {pulse=std::make_shared<BladeShieldPulse>();pulse->actor=actor;pulse->skill=skill;}
    return advanceBladeShield(*pulse,actor.tick);
}
bool System::bladeShieldStarted(const std::shared_ptr<BladeShieldPulse> &pulse) const {return pulse && pulse->prepared && !pulse->complete;}
DomainResult<> System::bladeShieldEvent(const FrameEvent &event,uint64_t tick) {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,event.owner.unit);
    const auto retained=bladeShieldPulses_.find(event.handle);
    const bool alive=caster && caster->programs() && (caster->player?caster->player->entered && caster->player->persistent.player.hp>0:caster->monster->alive());
    if(!alive &&
        (retained==bladeShieldPulses_.end() || !retained->second.second || !retained->second.second->prepared)) {
        ports_.frameEvents.cancel(event.handle);bladeShieldPulses_.erase(event.handle);return {DomainStatus::Applied,std::monostate{}};
    }
    auto [entry,inserted]=bladeShieldPulses_.try_emplace(event.handle,std::pair{event.owner,std::shared_ptr<BladeShieldPulse>{}});
    if(inserted) {
        const auto *states=unitEffects(caster->id());const ActiveCombatEffect *effect=nullptr;
        if(states) for(const auto &candidate:states->entries()) if(candidate.spec.source.kind==CombatEffectSource::Skill &&
            candidate.spec.source.definition==event.customId && states->hasState(candidate.spec.state.id,tick)) {effect=&candidate;break;}
        auto input=caster->formulaInput(*this,tick);
        if(!effect || !input.referenceRanks->effective.contains(event.customId)) {
            ports_.frameEvents.cancel(event.handle);bladeShieldPulses_.erase(entry);return {DomainStatus::Applied,std::monostate{}};
        }
        auto pulse=std::make_shared<BladeShieldPulse>();
        pulse->actor=caster->context(ports_.areas,tick);
        pulse->skill=skills::evaluateNativeSkill(*caster,*this,event.customId,effect->spec.source.level,tick);entry->second.second=std::move(pulse);
    }
    const auto pulse=entry->second.second;const auto advanced=advanceBladeShield(*pulse,tick);if(!advanced) return advanced;
    FrameEventQueue::PreparedInsertion next;
    const auto *states=caster?unitEffects(caster->id()):nullptr;
    if(alive && states && pulse->skill.assassin && states->hasState(pulse->skill.assassin->state.id,tick)) {
        auto input=caster->formulaInput(*this,tick);SkillFormulaValues values(*caster->programs()->formulas,input);
        const auto period=values.calculation(FormulaDomain::Skill,pulse->skill.sourceId,pulse->skill.rank,"perdelay");
        if(!period) throw std::logic_error("Unavailable native Blade Shield period");
        next=ports_.frameEvents.prepareInsertion({{},event.owner,FrameEventType::PeriodicSkills,skillPeriodicTick(tick,*period),
            skillPeriodicFrame(nativeNarrow(uint32_t(tick)),*period)==-1,event.handler,event.customId,pulse->skill.rank,event.payload});
    }
    ports_.frameEvents.cancel(event.owner,FrameEventType::PeriodicSkills,event.customId);
    if(next.handle()) ports_.frameEvents.publish(std::move(next));bladeShieldPulses_.erase(event.handle);
    return {DomainStatus::Applied,std::monostate{}};
}
}
