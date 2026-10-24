#include "dark_pact.hpp"
#include "../system.hpp"
#include "../attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::effects {
DomainResult<> System::pd2DarkPact(const ActorContext &actor,const SkillCastSpec &skill,Vec center,EntityId selected,
    std::shared_ptr<Pd2DarkPactPulse> &retained) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    const auto *area=ports_.areas.find(actor.area);
    if(!caster || !caster->player || !caster->programs() || !caster->programs()->pd2 || !skill.curse ||
       !area || area->generation!=actor.areaGeneration || caster->area()!=actor.area) return {DomainStatus::InvalidActor,{}};
    if(!retained) retained=std::make_shared<Pd2DarkPactPulse>();
    auto &pulse=*retained;const auto &rules=*caster->programs();
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!pulse.prepared) {
        if(caster->player->persistent.player.hp<=0) return {DomainStatus::Unavailable,{}};
        auto origin=center+area->definition.origin;
        const auto target=participants.find(selected);
        if(target.id()) origin=target.position()+ports_.areas.at(target.player?target.player->area:target.monster->area).definition.origin;
        pulse.targets=nativeAuraCandidateRecipients({actor.actor,actor.area,skill.curse->radius,uint32_t(skill.curse->targetFilter),false,
            origin,NativeAuraTargetMode::TargetRange,actor.tick});
        pulse.prepared=true;
    }
    if(!pulse.paid) {
        const auto &owner=*caster->player;
        if(owner.persistent.player.mana<skill.manaCost) return {DomainStatus::Unavailable,{}};
        transactions::CharacterEdit debit{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};
        debit.player.mana-=skill.manaCost;debit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};debit.charge=skill.charge;
        auto plan=ports_.transactions.prepare(std::move(debit));if(!plan) return {plan.status,{}};
        const auto paid=ports_.transactions.commit(std::move(*plan.value));if(!paid) return paid;
        pulse.paid=true;
    }
    // Withdraw a specific posted list, or allocate the final one-frame list.
    // Resource traces preserve RemoveState callbacks and synchronous FullStats
    // changes; retries never repeat a committed withdrawal or damage pulse.
    const auto changeState=[&](EntityId id,std::optional<EffectHandle> remove,const CombatEffectSpec *post)->DomainResult<> {
        const auto recipient=participants.find(id);if(!recipient.id()) return done();
        const auto mutate=[&](CombatEffectSet &states,const auto &observe) {
            std::vector<RemovedCombatEffect> removed;
            if(remove) removed=states.remove(*remove,observe);
            if(post) {
                const auto applied=states.apply(*post,actor.tick,observe);
                if(!applied.accepted) throw std::logic_error("PD2 Dark Pact state allocation refused");
                removed.insert(removed.end(),applied.removed.begin(),applied.removed.end());
            }
            return removed;
        };
        if(recipient.player) {
            const auto &peer=*recipient.player;auto next=state_;auto &states=next.players[id].states;
            PlayerResourceTrace resources(peer,actor.tick,{},ports_.areas.statTime(peer.area));
            const auto removed=mutate(states,[&]{resources.sample(states,true);});
            transactions::CharacterEdit edit{{peer.player,id,peer.area,ports_.areas.at(peer.area).generation,0,actor.tick},
                peer.inventoryRevision,peer.characterRevision,resources.record()};
            edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(states,actor.tick);
            for(const auto &entry:removed) if(!states.hasState(entry.effect.spec.state.id,actor.tick))
                edit.publicFacts.emplace_back(StateFact{id,0,peer.area,entry.effect.spec.state.id,false});
            if(post) {
                const auto *installed=states.findState(post->state.id);
                if(!installed) throw std::logic_error("Missing PD2 Dark Pact player list");
                edit.publicFacts.emplace_back(StateFact{id,0,peer.area,post->state.id,true,nativeEffectStats(rules,installed->spec)});
            }
            auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
            const auto committed=ports_.transactions.commit(std::move(*plan.value));if(committed) publishPlayers(next.players);
            return committed;
        }
        const auto &body=*recipient.monster;
        UnitEffect next;if(const auto found=units_.find(id);found!=units_.end()) next=found->second;next.area=body.area;
        MonsterResourceTrace resources(body,actor.tick,&next.states);
        const auto removed=mutate(next.states,[&]{resources.sample(next.states,true);});
        AuraUnitPublication publication;
        for(const auto &entry:removed) if(!next.states.hasState(entry.effect.spec.state.id,actor.tick)) {
            next.nativeStats.erase(entry.effect.spec.state.id);
            publication.facts.emplace_back(StateFact{id,1,body.area,entry.effect.spec.state.id,false});
        }
        if(post) {
            const auto *installed=next.states.findState(post->state.id);
            if(!installed) throw std::logic_error("Missing PD2 Dark Pact monster list");
            auto stats=nativeEffectStats(*body.rule.stats,installed->spec);next.nativeStats[post->state.id]=stats;
            publication.facts.emplace_back(StateFact{id,1,body.area,post->state.id,true,std::move(stats)});
        }
        resources.sample(next.states);prepareUnitEffects(publication,id,next,resources.take(),actor.tick);
        if(!validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
        const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},body.area},std::move(publication.facts)});
        if(!sent) return {sent.status,{}};
        publishAuraUnits(publication);return done();
    };
    while(pulse.next<pulse.targets.size()) {
        const auto id=pulse.targets[pulse.next];const auto recipient=participants.find(id);
        if(!recipient.id()) {pulse.target.reset();++pulse.next;continue;}
        if(!pulse.target) {
            const auto region=recipient.player?recipient.player->area:recipient.monster->area;
            if(!recipient.aliveIn(region) || !participants.canHarm(actor.actor,id) ||
               (recipient.monster && (!recipient.monster->rule.curseable || !recipient.monster->rule.stateTarget.acceptsCurseList() ||
                (recipient.monster->rule.enchantment && recipient.monster->rule.enchantment->has(38))))) {++pulse.next;continue;}
            const auto *states=unitEffects(id);if(!states) {++pulse.next;continue;}
            Pd2DarkPactPulse::Target target;target.id=id;
            const auto source=unitStatValues(actor.actor,actor.tick);if(!source) return {DomainStatus::Unavailable,{}};
            target.allowance=nativeAdd(logicalStatValue(*source,*rules.stats,"max_curses"),1);
            const StatKey slots{rules.stats->names.at("curse_slots"),0};EffectHandle oldest{};
            for(auto entry=states->entries().rbegin();entry!=states->entries().rend();++entry) if(entry->activeAt(actor.tick) && entry->spec.state.curse) {
                ++target.count;
                if(entry->spec.nativeValues) if(const auto found=entry->spec.nativeValues->find(slots);found!=entry->spec.nativeValues->end())
                    target.allowance=std::max(target.allowance,found->second);
                // Native scans newest to oldest, retaining the last finite
                // list. It does not sort by the nearest expiration deadline.
                if(uint32_t(entry->nativeExpireFrame.value_or(0))<INT32_MAX) oldest=entry->handle;
            }
            if(!target.count) {++pulse.next;continue;}
            for(auto entry=states->entries().rbegin();entry!=states->entries().rend();++entry)
                if(entry->activeAt(actor.tick) && entry->spec.state.curse && entry->handle!=oldest) {target.consumed=entry->handle;break;}
            pulse.target=std::move(target);
        }
        auto &target=*pulse.target;
        if(!target.withdrawn) {
            if(target.consumed) {const auto changed=changeState(id,target.consumed,nullptr);if(!changed) return changed;}
            target.withdrawn=true;
        }
        if(!target.damage) {
            const auto source=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);if(!source) return {DomainStatus::Stale,{}};
            auto random=source->random();const auto expected=random;
            const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
                return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
            };
            auto input=source->formulaInput(*this,actor.tick,draw);SkillFormulaValues values(*rules.formulas,input);
            const auto radius=values.calculation(FormulaDomain::Skill,skill.sourceId,skill.rank,"calc1");
            if(!radius) throw std::logic_error("Missing PD2 Dark Pact radius");
            MissileHitDamage damage;rollNativeSkillElement(damage,values,skill.sourceId,skill.rank,random);
            // Current 0x36C443..0x36C496 scales unsigned magic damage by
            // 1 + (curse count - 1) * 1.1, with double truncation/saturation.
            const double amount=(1.0+double(target.count-1)*1.1)*double(uint32_t(damage.channels[1]));
            damage.channels[1]=int32_t(amount<0?0u:amount>=double(UINT32_MAX)?UINT32_MAX:uint32_t(std::trunc(amount)));
            auto pulse=std::make_shared<NativeAreaPulse>();auto &area=pulse->spec;
            area.source=actor.actor;area.area=actor.area;area.skill=skill.sourceId;area.rank=skill.rank;
            area.center=recipient.position()+ports_.areas.at(recipient.player?recipient.player->area:recipient.monster->area).definition.origin-
                ports_.areas.at(actor.area).definition.origin;
            area.radius=nativeAdd(target.count-1,*radius);area.filter=0x8583;area.hitFlags=1;area.resultFlags=5;area.damage=damage;
            target.damage=std::move(pulse);target.seed={expected,random};
        }
        if(!target.seedCommitted) {
            const auto paid=ports_.transactions.consumeRandom(actor,target.seed.first,target.seed.second);if(!paid) return paid;
            target.seedCommitted=true;
        }
        const auto damaged=nativeArea(target.damage->spec,actor.tick,target.damage);if(!damaged) return damaged;
        CombatEffectSpec state;state.state=skill.curse->state;state.source={CombatEffectSource::Skill,actor.actor,skill.sourceId,skill.rank};
        state.nativeState=true;state.nativeValues.emplace();state.stacking=EffectStacking::Independent;
        state.duration=1;state.nativeExpiration=NativeStateExpiration::EvenZeroDuration;state.refreshPassivesOnRemoval=true;
        state.pd2CurseSlots=Pd2CurseSlots{target.allowance,{rules.stats->names.at("curse_slots"),0},0,true};
        const auto posted=changeState(id,{},&state);if(!posted) return posted;
        pulse.target.reset();++pulse.next;
    }
    return done();
}
}
