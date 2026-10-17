#include "system.hpp"
#include "runtime.hpp"
#include "assassin_kick.hpp"
#include "native_caster.hpp"
#include "evaluation.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::skills {
DomainStatus System::allocateAssassinKick(Release &pending,const ActorContext &actor,bool held,bool knockback) {
    auto &kick=*pending.assassinKick;const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    if(!kick.occurrence) {
        if(runtime_->nativeKickOccurrence>=(uint64_t{1}<<53)-1) return DomainStatus::Capacity;
        const auto occurrence=(uint64_t{1}<<54)|(runtime_->nativeKickOccurrence+1);
        auto contact=std::make_shared<combat::NativeSkillContact>();contact->kick=true;contact->kickKnockback=knockback;
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};const auto target=participants.find(pending.unit);
        if(!target.id()) return DomainStatus::Unavailable;
        missiles::Spawn request{actor,pending.skill,{},target.position(),true};
        request.deathTrigger=!held;request.nativeMeleeOccurrence=occurrence;request.nativeMeleeContact=contact;request.nativeMeleeHeld=held;
        WeaponSkillDamage weapon;weapon.weapon=caster->player?caster->player->totals.equipment.weapons[0]:caster->monster->petWeapon.value_or(WeaponDamage{});
        weapon.level=caster->level();request.weapon=std::move(weapon);
        const auto queued=ports_.missiles.directContact(request,{pending.unit});if(!queued) return queued.status;
        kick.contact=std::move(contact);kick.occurrence=occurrence;++runtime_->nativeKickOccurrence;
    }
    pending.nativeCallbackStarted=!held;
    const auto result=held?ports_.combat.allocateNativeMelee(actor.actor,*kick.occurrence,{actor.tick}):
        ports_.combat.advanceAuraPulse(actor.actor,*kick.occurrence,{actor.tick});
    if(result==StepStatus::Blocked) return DomainStatus::Capacity;
    if(kick.contact->cancelled) return DomainStatus::Unavailable;
    if(held && !kick.contact->heldPrepared) return DomainStatus::Unavailable;
    pending.nativeCallbackStarted=false;return DomainStatus::Applied;
}
DomainResult<> System::startAssassinKick(Release &pending,const ActorContext &actor) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !pending.skill.assassin) return {DomainStatus::InvalidActor,{}};
    const auto role=pending.skill.assassin->program;
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    auto old=runtime_->assassinStarts.find(actor.actor);
    if(old!=runtime_->assassinStarts.end() && (old->second.actor.sequence!=actor.sequence || old->second.skill.sourceId!=pending.skill.sourceId || old->second.unit!=pending.unit)) {
        if(old->second.assassinKick && old->second.assassinKick->occurrence) ports_.combat.discardNativeMelee(actor.actor,*old->second.assassinKick->occurrence);
        runtime_->assassinStarts.erase(old);
    }
    if(!pending.assassinKick) {
        auto [entry,created]=runtime_->assassinStarts.try_emplace(actor.actor,pending);
        if(created) entry->second.assassinKick=std::make_shared<AssassinKickExecution>();
        pending.assassinKick=entry->second.assassinKick;
        if(!created) pending.skill=entry->second.skill;
    }
    auto &kick=*pending.assassinKick;
    if(kick.startPrepared) {
        if(kick.contact && kick.contact->cancelled) {runtime_->assassinStarts.erase(actor.actor);return {DomainStatus::Unavailable,{}};}
        return done();
    }
    const auto fail=[&](DomainStatus status) {
        if(!isDeferred(status)) {
            if(kick.occurrence) ports_.combat.discardNativeMelee(actor.actor,*kick.occurrence);
            runtime_->assassinStarts.erase(actor.actor);
        }
        return DomainResult<>{status,{}};
    };
    if(role==AssassinProgram::DragonFlight) {
        kick.phase=AssassinKickExecution::Phase::FlightTeleport;kick.startPrepared=true;
        return done();
    }
    auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
    if(role==AssassinProgram::DragonTail && !kick.modeStats) {
        CombatEffectSpec mode;mode.stacking=EffectStacking::ReplaceSource;mode.nativeDirectAllocation=true;mode.nativeValues.emplace();
        mode.source={CombatEffectSource::Skill,caster->id(),pending.skill.sourceId,pending.skill.rank};
        mode.modeActionRevision=caster->player?actor.sequence:caster->monster->nativeActionRevision+1;
        const std::array<StatValue,1> writes{{{StatKey{caster->programs()->stats->names.at("attackrate"),0},values.skillNumber(pending.skill.sourceId,"param4")}}};
        if(caster->player) {
            const auto *player=caster->player;
            auto states=ports_.effects.skillState(actor,std::move(mode),writes);if(!states) return fail(states.status);
            transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,states.value->character};
            edit.transient=states.value->transient;edit.resourceBase=states.value->resourceBase;edit.publicFacts=states.value->publicFacts;
            auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return fail(plan.status);
            const auto applied=ports_.transactions.commit(std::move(*plan.value));if(!applied) return fail(applied.status);
            ports_.effects.commit(std::move(*states.value));
        } else {
            auto cast=pending.skill;cast.appliedEffect=std::move(mode);cast.nativeEffectWrites.assign(writes.begin(),writes.end());
            const auto applied=ports_.effects.unitSkill(caster->id(),cast,actor.tick);if(!applied) return fail(applied.status);
        }
        kick.modeStats=true;
    }
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};const auto target=participants.find(pending.unit);
    if(!target.aliveIn(actor.area) || !participants.canHarm(actor.actor,pending.unit)) return fail(DomainStatus::Unavailable);
    const bool reachable=unitMeleeReach(ports_.areas.at(actor.area).definition.collision,caster->position(),caster->player?2:caster->monster->rule.size,
        target.position(),target.size(),caster->player?caster->player->totals.equipment.weapons[0].rangeAdder:caster->monster->rule.meleeRange);
    if(!reachable) return fail(DomainStatus::Unavailable);
    if(role==AssassinProgram::DragonTalon && !kick.parameterPrepared) {
        const auto count=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc1");
        if(!count) throw std::logic_error("Missing original Dragon Talon kick count");
        kick.parameter=nativeSubtract(*count,1);kick.parameterPrepared=true;
        state_.skillParameter(caster->id(),{pending.skill.sourceId,pending.skill.charge?pending.skill.charge->item.id:EntityId{}},0,kick.parameter);
    }
    const auto allocated=allocateAssassinKick(pending,actor,true,role==AssassinProgram::DragonTalon && !kick.parameter);
    if(allocated!=DomainStatus::Applied) return fail(allocated);
    if(role==AssassinProgram::DragonTail && !kick.contact->hit) return fail(DomainStatus::Unavailable);
    kick.copiedPhysical=kick.contact->allocatedPhysical;kick.phase=AssassinKickExecution::Phase::InitialDrain;kick.startPrepared=true;
    return done();
}
DomainStatus System::activateAssassinKick(Release &pending,const ActorContext &actor,Vec targetPosition) {
    if(!pending.assassinKick) return DomainStatus::Unavailable;
    auto &kick=*pending.assassinKick;using Phase=AssassinKickExecution::Phase;
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    const auto role=pending.skill.assassin->program;
    const auto slot=NativeSkillSlot{pending.skill.sourceId,pending.skill.charge?pending.skill.charge->item.id:EntityId{}};
    const auto drain=[&]() {
        pending.nativeCallbackStarted=true;
        if(ports_.combat.drainNativeMelee(caster->id(),*kick.occurrence,{actor.tick})==StepStatus::Blocked) return DomainStatus::Capacity;
        pending.nativeCallbackStarted=false;kick.occurrence.reset();kick.contact.reset();return DomainStatus::Applied;
    };
    for(;;) {
        if(kick.phase==Phase::FlightTeleport) {
            const auto &area=ports_.areas.at(actor.area).definition;
            if(!area.teleportMode || (area.teleportMode==2 && !area.collision.collisionSegment(caster->position(),targetPosition,0x804)))
                return DomainStatus::Unavailable;
            const auto global=targetPosition+area.origin;
            const auto room=ports_.areas.roomAtPosition(actor.area,caster->position(),int(global.x),int(global.y));
            if(!room) return DomainStatus::Unavailable;
            const auto sample=ports_.spatial.nativeCollisionSample(0x1c09);
            const auto free=ports_.areas.nativeFreeCoordinates(*room,int(global.x),int(global.y),caster->monster?caster->monster->rule.size:2,0x1c09,sample);
            if(!free || free->room.area!=actor.area) return DomainStatus::Unavailable;
            const auto moved=caster->monster?ports_.monsters.teleport(caster->id(),free->position,actor.tick,0,true):
                ports_.travel.teleport(actor,{actor.area,actor.areaGeneration,free->position},Fixed8{});
            if(!moved) return moved.status;
            kick.phase=Phase::FlightAllocation;
            if(pending.weaponHits.size()<2) return DomainStatus::Unavailable;
            kick.nextEvent=state_.casts.contains(actor.actor)?state_.casts.at(actor.actor).started+uint64_t(pending.weaponHits[1]):actor.tick+uint64_t(pending.weaponHits[1]-pending.weaponHits[0]);
            pending.tick=kick.nextEvent;return DomainStatus::Applied;
        }
        if(kick.phase==Phase::FlightAllocation) {
            const auto status=allocateAssassinKick(pending,actor,false,false);if(status!=DomainStatus::Applied) return status;
            kick.occurrence.reset();kick.contact.reset();kick.phase=Phase::Complete;continue;
        }
        if(kick.phase==Phase::InitialDrain) {
            const auto status=drain();if(status!=DomainStatus::Applied) return status;
            kick.phase=role==AssassinProgram::DragonTail?Phase::TailArea:Phase::NextSelect;continue;
        }
        if(kick.phase==Phase::NextSelect) {
            kick.parameter=nativeSubtract(kick.parameter,1);state_.skillParameter(caster->id(),slot,0,kick.parameter);
            if(kick.parameter<0) {kick.phase=Phase::Complete;continue;}
            kick.knockback=false;
            if(kick.parameter<=0) {
                const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};const auto target=participants.find(pending.unit);
                if(!target.id()) return DomainStatus::Unavailable;
                auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
                std::string_view field;
                if(target.player || target.monster->hireling) field="calc4";
                else if(target.monster->rule.boss) field="calc3";
                else if(nativeUniqueMonster(target.monster->identity.rank)) field="calc2";
                const auto calculated=field.empty()?std::optional<int32_t>{100}:values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,field);
                if(!calculated) throw std::logic_error("Missing original Dragon Talon knockback chance");
                if(*calculated>0) {
                    const auto expected=caster->random();auto seed=expected;const bool knockback=rollRandom(seed)%100<uint32_t(*calculated);
                    const auto changed=caster->monster?ports_.monsters.consumeRandom(caster->id(),expected,seed):ports_.transactions.consumeRandom(actor,expected,seed);
                    if(!changed) {kick.parameter=nativeAdd(kick.parameter,1);state_.skillParameter(caster->id(),slot,0,kick.parameter);return changed.status;}
                    kick.knockback=knockback;
                }
            }
            kick.phase=Phase::NextAllocation;continue;
        }
        if(kick.phase==Phase::NextAllocation) {
            const auto status=allocateAssassinKick(pending,actor,false,kick.knockback);if(status!=DomainStatus::Applied) return status;
            kick.occurrence.reset();kick.contact.reset();kick.phase=Phase::NextDrain;continue;
        }
        if(kick.phase==Phase::NextDrain) {
            if(kick.parameter<=0) {kick.phase=Phase::Complete;continue;}
            // Do42 restarts its animation counter, not a new skill cast.
            // CltSt6/CltDo4 owns the matching client kick counter.
            if(caster->player) {
                const auto timeline=rewoundPlayerActionTimeline(pending.weaponFrames,pending.weaponSpeed,0,pending.animationEvents);
                auto prepared=state_.prepareTimeline(actor.actor,actor.tick,timeline);
                auto &cast=state_.casts.at(actor.actor);cast.timeline=prepared.generation;cast.until=actor.tick+uint64_t(timeline.end);
                state_.commitTimeline(std::move(prepared));
                pending.animationAnchor=int64_t(actor.tick);pending.tick=actor.tick+1;
            } else pending.tick=actor.tick+uint64_t(std::max(1,pending.weaponHits.empty()?1:pending.weaponHits.front()));
            kick.nextEvent=pending.tick;kick.phase=Phase::NextSelect;return DomainStatus::Applied;
        }
        if(kick.phase==Phase::TailArea) {
            if((caster->player?caster->player->persistent.player.hp<=0:!caster->monster->alive())) {kick.phase=Phase::Complete;continue;}
            auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
            const auto percent=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc1");
            const auto mastery=values.stat("passive_fire_mastery");
            if(!percent || !mastery) throw std::logic_error("Missing native Dragon Tail fire calculation");
            effects::NativeAreaSpec area;area.source=caster->id();area.area=actor.area;area.center=targetPosition;
            area.skill=pending.skill.sourceId;area.rank=pending.skill.rank;area.filter=0x8583;area.resultFlags=1;area.hitFlags=9;
            area.radius=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"aurarangecalc").value_or(0);
            area.damage.channels[2]=nativePercentage(kick.copiedPhysical,nativeAdd(*percent,*mastery),100);
            pending.nativeCallbackStarted=true;
            const auto result=ports_.effects.nativeArea(area,actor.tick,kick.area);if(!result) return result.status;
            pending.nativeCallbackStarted=false;kick.phase=Phase::Complete;continue;
        }
        return DomainStatus::Applied;
    }
}
}
