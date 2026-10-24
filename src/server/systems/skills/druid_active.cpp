#include "system.hpp"
#include "pd2/vengeance.hpp"
#include "pd2/corpses.hpp"
#include "runtime.hpp"
#include "native_caster.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "core/random.hpp"
#include <stdexcept>
namespace d2x::server::skills {
DomainStatus System::startDruidPeriodic(Release &pending,const ActorContext &actor) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !pending.skill.appliedEffect) return DomainStatus::Unavailable;
    const auto &native=caster->programs()->formulas->skills.at(pending.skill.sourceId);
    const auto deadline=nativeEventDeadline(actor.tick,nativeAdd(nativeNarrow(uint32_t(actor.tick)),native.number("param4")));
    const FrameEventOwner owner{caster->monster?FrameEventUnit::Monster:FrameEventUnit::Player,actor.actor};
    auto timer=ports_.frameEvents.prepareInsertion({{},owner,FrameEventType::ActiveState,deadline.due,deadline.continuous,0,
        pending.skill.sourceId,pending.skill.rank,pending.skill.charge?pending.skill.charge->item.id.value:0});
    auto random=caster->random();const auto parameter=uint16_t(rollRandom(random));
    if(caster->player) {
        auto effect=*pending.skill.appliedEffect;effect.source.entity=actor.actor;
        auto prepared=ports_.effects.skillState(actor,std::move(effect),pending.skill.nativeEffectWrites);if(!prepared) return prepared.status;
        const auto *player=caster->player;
        transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,prepared.value->character};
        edit.transient=prepared.value->transient;edit.resourceBase=prepared.value->resourceBase;edit.publicFacts=prepared.value->publicFacts;
        edit.random=transactions::UnitRandomEdit{player->combatRandom,random};
        if(!pending.manaPaid) {edit.player.mana-=pending.skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{pending.skill.manaCost,pending.skill.sourceId};edit.charge=pending.skill.charge;}
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return plan.status;
        const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed.status;
        ports_.effects.commit(std::move(*prepared.value));
    } else {
        const auto installed=ports_.effects.unitSkill(actor.actor,pending.skill,actor.tick,std::pair{caster->random(),random});
        if(!installed) return installed.status;
    }
    state_.skillParameter(actor.actor,{pending.skill.sourceId,EntityId{pending.skill.charge?pending.skill.charge->item.id.value:0}},0,parameter);
    ports_.frameEvents.cancel(owner,FrameEventType::ActiveState,pending.skill.sourceId);ports_.frameEvents.publish(std::move(timer));
    return DomainStatus::Applied;
}
DomainResult<> System::activeStateEvent(const FrameEvent &event,uint64_t tick) {
    if(event.handler==pd2CorpseTimerHandler) return pd2CorpseEvent(ports_,event,tick);
    if(event.handler==pd2VengeanceTimerHandler) return pd2VengeanceEvent(ports_,event,tick,runtime_->pd2Vengeance);
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(event.owner.type==FrameEventUnit::Monster) if(const auto *unit=ports_.monsters.find(event.owner.unit);
        unit && unit->rule.ai.kind==MonsterAiKind::Imp && event.customId==unit->rule.skillIds[0])
        return ports_.monsters.impRiderEvent(event,tick);
    const auto caster=nativeCaster(ports_.players,ports_.monsters,event.owner.unit);
    auto &retained=runtime_->druidActive;
    const auto finish=[&] {ports_.frameEvents.cancel(event.handle);retained.erase(event.handle);return done();};
    if(!caster || !caster->programs()) return finish();
    auto &entry=retained[event.handle];if(!entry) entry=std::make_shared<DruidActiveExecution>();auto &execution=*entry;
    const auto &rules=*caster->programs();const auto definition=rules.definitions.find(event.customId);
    if(definition==rules.definitions.end() || !definition->second.spec.druid) return finish();
    const auto &program=*definition->second.spec.druid;
    const auto &native=rules.formulas->skills.at(event.customId);
    const auto actor=caster->context(ports_.areas,tick);
    const auto &area=ports_.areas.at(caster->area());
    if(!execution.prepared) {
        if(event.customParameter<=0 || (caster->player?caster->player->persistent.player.hp<=0:!caster->monster->alive()) ||
           !ports_.effects.unitStates(caster->id(),tick).contains(program.state.id)) return finish();
        if(area.definition.town && !native.number("intown")) {
            const auto removed=ports_.effects.removeState(actor,program.state.id);if(!removed) return removed;
            return finish();
        }
        auto random=caster->random();
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {return low>=high?low:
            nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));};
        auto input=caster->formulaInput(ports_.effects,tick,draw);SkillFormulaValues values(*rules.formulas,input);
        const auto radius=values.calculation(FormulaDomain::Skill,event.customId,event.customParameter,"aurarangecalc");
        if(!radius || *radius<=0) return finish();
        if(!execution.rescheduled) {
            const auto deadline=nativeEventDeadline(tick,nativeAdd(nativeNarrow(uint32_t(tick)),native.number("param4")));
            auto next=ports_.frameEvents.prepareInsertion({{},event.owner,FrameEventType::ActiveState,deadline.due,deadline.continuous,0,event.customId,event.customParameter,event.payload});
            for(const auto &previous:ports_.frameEvents.events(event.owner))
                if(previous.handle!=event.handle && previous.type==FrameEventType::ActiveState && previous.customId==event.customId)
                    ports_.frameEvents.cancel(previous.handle);
            ports_.frameEvents.publish(std::move(next));execution.rescheduled=true;
        }
        if(area.definition.town) return finish();
        execution.area={caster->id(),caster->area(),caster->position(),*radius,event.customId,event.customParameter,
            uint32_t(native.number("aurafilter")),uint32_t(native.number("hitflags")),uint16_t(native.number("resultflags"))};
        if(program.activeFunction==145) execution.area.damage=rollNativeSkillDamage(values,event.customId,event.customParameter,random);
        else if(program.activeFunction==146) {
            const bool equipped=rules.pd2 && input.stats && input.stats->get({rules.stats->names.at("item_skillonequip"),uint16_t(event.customId)})>0;
            if((!equipped && (!input.referenceRanks || !input.referenceRanks->effective.contains(event.customId))) || definition->second.spec.missileId<=0) return finish();
            random=initialRandom(uint32_t(state_.skillParameter(caster->id(),{event.customId,EntityId{event.payload}},0)));
            execution.nextParameter=int(rollRandom(random)%1000000);
            for(int attempt=0;attempt<5;++attempt) {
                const int x=nativeAdd(int(caster->position().x+area.definition.origin.x),nativeSubtract(int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeMultiply(*radius,2),1)))),*radius));
                const int y=nativeAdd(int(caster->position().y+area.definition.origin.y),nativeSubtract(int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeMultiply(*radius,2),1)))),*radius));
                const Vec destination{float(x)-area.definition.origin.x,float(y)-area.definition.origin.y};
                if(!area.definition.collision.collisionSegment(caster->position(),destination,0x45)) continue;
                const auto room=ports_.areas.roomAtPosition(caster->area(),caster->position(),x,y);if(!room) continue;
                const auto &targetArea=ports_.areas.at(room->area);
                const Vec point{float(x)-targetArea.definition.origin.x,float(y)-targetArea.definition.origin.y};
                if(!targetArea.definition.collision.missileSegment(point,point,{1,1})) continue;
                execution.missile=missiles::NativeSpawn{caster->id(),room->area,targetArea.generation,tick,point,point,definition->second.spec.missileId,event.customId,event.customParameter};
                execution.missile->copyMonsterAttackBonus=true;break;
            }
        } else return finish();
        execution.random=std::pair{caster->random(),random};execution.prepared=true;
    }
    if(!execution.consumed) {
        if(execution.missile) {
            execution.missile->ownerSeedEdit=execution.random;
            const std::array<DomainFact,1> extra{SkillPulseFact{caster->id(),{},uint8_t(caster->monster?1:0),1,execution.missile->area,event.customId,event.customParameter,execution.missile->position,0,uint32_t(int(execution.missile->position.x+ports_.areas.at(execution.missile->area).definition.origin.x)),uint32_t(int(execution.missile->position.y+ports_.areas.at(execution.missile->area).definition.origin.y))}};
            auto prepared=ports_.missiles.prepareNativeBirth(std::span{&*execution.missile,size_t{1}},0,extra);if(!prepared) return {prepared.status,{}};
            const auto born=ports_.missiles.commitNativeBirth(std::move(*prepared.value));if(!born) return {born.status,{}};
        } else {
            const auto consumed=caster->monster?ports_.monsters.consumeRandom(caster->id(),execution.random->first,execution.random->second):
                ports_.transactions.consumeRandom(actor,execution.random->first,execution.random->second);
            if(!consumed) return consumed;
        }
        if(program.activeFunction==146) state_.skillParameter(caster->id(),{event.customId,EntityId{event.payload}},0,execution.nextParameter);
        execution.consumed=true;
    }
    if(program.activeFunction==145) {
        const auto applied=ports_.effects.nativeArea(execution.area,tick,execution.pulse);if(!applied) return applied;
    }
    return finish();
}
}
