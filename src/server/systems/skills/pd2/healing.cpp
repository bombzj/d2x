#include "../system.hpp"
#include "../native_caster.hpp"
#include "execution.hpp"
#include "server/systems/actions/release.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::skills {
DomainStatus System::activatePd2HolyLight(Release &pending,const ActorContext &actor) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto selected=participants.find(pending.unit);
    if(!pending.pd2Execution) pending.pd2Execution=std::make_shared<Pd2CastExecution>();
    auto &execution=*pending.pd2Execution;
    const auto &rules=*caster->programs();
    auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*rules.formulas,input);
    const auto formula=[&](std::string_view field) {
        const auto result=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,field);
        if(!result) throw std::logic_error("Missing PD2 Holy Light formula: "+std::string(field));
        return *result;
    };
    if(!execution.holyLight) {
        if(!selected.id()) return DomainStatus::Unavailable;
        Pd2CastExecution::HolyLight pulse;
        pulse.healing={nativeShiftLeft(formula("cltcalc2"),8),nativeShiftLeft(formula("cltcalc3"),8)};
        const auto minimum=values.parameter(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"enms");
        const auto maximum=values.parameter(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"exms");
        if(!minimum || !maximum) throw std::logic_error("Missing PD2 Holy Light elemental range");
        pulse.damage={*minimum,*maximum};
        // Current Do176 / callback 0x304580: the selected unit determines
        // which half of the area effect is reduced. Every recipient then
        // receives healing or magic damage according to its own allegiance.
        auto &halved=participants.canHarm(actor.actor,selected.id())?pulse.healing:pulse.damage;
        for(auto &amount:halved) amount/=2;
        const auto &area=ports_.areas.at(actor.area);
        const auto filter=uint32_t(rules.formulas->skills.at(pending.skill.sourceId).number("aurafilter"));
        pulse.targets=ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,formula("aurarangecalc"),
            filter?filter:0x583u,false,
            selected.position()+area.definition.origin,effects::NativeAuraTargetMode::TargetRange,actor.tick});
        std::erase(pulse.targets,actor.actor);
        execution.holyLight=std::move(pulse);
    }
    auto &pulse=*execution.holyLight;
    if(!pending.manaPaid) {
        if(caster->player) {
            const auto &player=*caster->player;
            const auto paid=ports_.transactions.release(actor,player.characterRevision,pending.skill.manaCost,{},pending.skill.charge,{},{},pending.skill.sourceId);
            if(!paid) return paid.status;
        }
        pending.manaPaid=true;
    }
    pending.nativeCallbackStarted=true;
    while(pulse.next<pulse.targets.size()) {
        const auto target=participants.find(pulse.targets[pulse.next]);
        if(!target.aliveIn(actor.area)) {++pulse.next;continue;}
        if(!pulse.amount) {
            pulse.hostile=participants.canHarm(actor.actor,target.id());
            const auto &range=pulse.hostile?pulse.damage:pulse.healing;
            const auto current=nativeCaster(ports_.players,ports_.monsters,actor.actor);
            if(!current) return DomainStatus::Stale;
            const auto expected=current->random();auto random=expected;
            const auto amount=nativeAdd(range[0],int32_t(limitedRandom(random,uint32_t(nativeSubtract(range[1],range[0])))));
            const auto accepted=current->monster?ports_.monsters.consumeRandom(actor.actor,expected,random):ports_.transactions.consumeRandom(actor,expected,random);
            if(!accepted) return accepted.status;
            pulse.amount=amount;
        }
        if(pulse.hostile) {
            if(!pending.directSkill) {
                pending.directSkill=std::make_shared<effects::NativeAreaPulse>();
                auto &spec=pending.directSkill->spec;
                spec.source=actor.actor;spec.area=actor.area;spec.skill=pending.skill.sourceId;spec.rank=pending.skill.rank;
                spec.targets=std::vector<EntityId>{target.id()};spec.damage.channels[1]=*pulse.amount;spec.resultFlags=1;
                if(target.monster && target.monster->rule.undead && target.id()!=pending.unit) spec.resultFlags|=8;
            }
            const auto hit=ports_.effects.nativeArea(pending.directSkill->spec,actor.tick,pending.directSkill);
            if(!hit) return hit.status;
            pending.directSkill.reset();
        } else if(*pulse.amount>0) {
            if(target.player) {
                const auto &patient=*target.player;
                const ActorContext context{patient.player,patient.actor,patient.area,ports_.areas.at(patient.area).generation,0,actor.tick};
                const auto healed=ports_.transactions.resources(context,patient.characterRevision,
                    Fixed8::fromRaw(nativeChecked(std::min<int64_t>(patient.totals.character.maxLifeFixed.raw(),int64_t(patient.persistent.player.hp.raw())+*pulse.amount))),
                    patient.persistent.player.mana,patient.persistent.player.stamina);
                if(!healed) return healed.status;
            } else {
                const auto healed=ports_.monsters.heal(target.id(),*pulse.amount);if(!healed) return healed.status;
            }
        }
        pulse.amount.reset();++pulse.next;
    }
    if(!pulse.overlayPublished) {
        const auto recipient=participants.find(pending.unit);
        const auto overlay=rules.formulas->skills.at(pending.skill.sourceId).serverOverlay;
        if(overlay>=0 && recipient.id()) {
            const auto posted=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},
                {OverlayFact{pending.unit,uint8_t(recipient.player?0:1),actor.area,overlay}}});
            if(!posted) return posted.status;
        }
        pulse.overlayPublished=true;
    }
    pending.nativeCallbackStarted=false;return DomainStatus::Applied;
}
}
