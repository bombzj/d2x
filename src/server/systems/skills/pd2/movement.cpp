#include "../system.hpp"
#include "../native_caster.hpp"
#include "server/systems/actions/release.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/movement.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "server/systems/combat/requests.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/combat/weapon_snapshot.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "gameplay/skills/leap_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::skills {
DomainStatus System::activatePd2Joust(Release &pending,const ActorContext &actor,Vec target) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !pending.unit) return DomainStatus::InvalidActor;
    const auto &rules=*caster->programs();
    if(!pending.pulses) {
        const auto *states=ports_.effects.unitEffects(actor.actor);
        if(states && states->hasState(214,actor.tick)) return DomainStatus::Unavailable;
        const auto &area=ports_.areas.at(actor.area).definition;
        const auto landing=ports_.travel.pd2TeleportLanding(actor.area,caster->position(),target,caster->monster?caster->monster->rule.size:2);
        if(!landing) return landing.status;
        target=*landing.value;
        const auto global=target+area.origin;
        const auto room=ports_.areas.roomAtPosition(actor.area,caster->position(),int(global.x),int(global.y));
        if(!room) return DomainStatus::Unavailable;
        const auto free=ports_.areas.nativeFreeCoordinates(*room,int(global.x),int(global.y),caster->monster?caster->monster->rule.size:2,
            0x1c09,ports_.spatial.nativeCollisionSample(0x1c09));
        if(!free || free->room.area!=actor.area) return DomainStatus::Unavailable;
        const auto moved=caster->monster?ports_.monsters.teleport(caster->id(),free->position,actor.tick,0,true):
            ports_.travel.teleport(actor,{actor.area,actor.areaGeneration,free->position},Fixed8{});
        if(!moved) return moved.status;
        pending.pulses=1;
    }
    pending.nativeCallbackStarted=true;
    if(pending.pulses==1) {
        auto attack=pending.skill;attack.effect=SkillBehavior::WeaponProjectile;attack.weapon.emplace();attack.weapon->doFunction=162;
        const auto current=nativeCaster(ports_.players,ports_.monsters,actor.actor);
        if(!current) return DomainStatus::Stale;
        WeaponSkillDamage weapon;weapon.weapon=current->player?current->player->totals.equipment.weapons[0]:current->monster->petWeapon.value_or(WeaponDamage{});
        weapon.level=current->level();
        missiles::Spawn request{actor,attack,{},target,pending.manaPaid || bool(current->monster)};request.weapon=std::move(weapon);
        request.nativeMeleeContact=pending.nativeContact;
        const auto queued=ports_.missiles.directContact(request,{pending.unit});if(!queued) return queued.status;
        pending.manaPaid=true;pending.nativeMeleeOccurrence=queued.value->occurrence;pending.pulses=2;
    }
    if(pending.pulses==2) {
        if(!pending.nativeMeleeOccurrence) return DomainStatus::Unavailable;
        if(ports_.combat.advanceAuraPulse(actor.actor,*pending.nativeMeleeOccurrence,{actor.tick})==StepStatus::Blocked) return DomainStatus::Capacity;
        pending.nativeMeleeOccurrence.reset();pending.pulses=3;
    }
    if(pending.skill.pd2->state.id>=0) {
        const auto current=nativeCaster(ports_.players,ports_.monsters,actor.actor);if(!current) return DomainStatus::Stale;
        auto input=current->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*rules.formulas,input);
        const auto length=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"auralencalc");
        if(!length) throw std::logic_error("Missing PD2 Joust buff duration");
        auto buff=pending.skill;buff.manaCost=buff.startMana=0;buff.charge.reset();
        CombatEffectSpec effect;effect.state=pending.skill.pd2->state;effect.nativeState=true;effect.nativeValues.emplace();
        effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;effect.duration=uint32_t(*length);effect.stacking=EffectStacking::SkillLevel;
        effect.source={CombatEffectSource::Skill,actor.actor,buff.sourceId,buff.rank};
        buff.appliedEffect=std::move(effect);buff.nativeEffectWrites=values.auraStats(buff.sourceId,buff.rank,6,true);
        const auto applied=current->monster?ports_.effects.unitSkill(actor.actor,buff,actor.tick):ports_.effects.skill(actor,buff);
        if(!applied) return applied.status;
    }
    pending.nativeCallbackStarted=false;return DomainStatus::Applied;
}
DomainStatus System::activatePd2Leap(Release &pending,const ActorContext &actor,Vec landing) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->player || !caster->programs()) return DomainStatus::InvalidActor;
    const auto &owner=*caster->player;const auto &rules=*caster->programs();
    auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*rules.formulas,input);
    const auto formula=[&](std::string_view field) {
        const auto result=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,field);
        if(!result) throw std::logic_error("Missing PD2 Leap formula");
        return *result;
    };
    if(!pending.leaping) {
        if(!ports_.areas.at(actor.area).definition.collision.walkable(landing,playerMovement)) return DomainStatus::Unavailable;
        // Do153 (RVA 0x3AEE70) replaces the player's fixed leap speed.
        const auto velocity=std::max(0xb00,nativeAdd(0xb00,nativeMultiply(formula("calc2"),0x50)));
        pending.chargeSpeed=float(velocity)*25.f/4096.f;
        auto skill=pending.skill;CombatEffectSpec moving;moving.state=skill.leap->moving;
        moving.duration=uint64_t(std::ceil((landing-owner.position).length()*25.f/pending.chargeSpeed))+5;
        skill.appliedEffect=std::move(moving);
        const auto posted=ports_.effects.skill(actor,skill);if(!posted) return posted.status;
        pending.leaping=pending.manaPaid=true;return DomainStatus::Applied;
    }
    if(pending.motionFrame!=actor.tick && !pending.motionFinished) {
        const auto moved=ports_.movement.leapStep(actor,landing,pending.chargeSpeed);if(!moved) return moved.status;
        pending.motionFrame=actor.tick;pending.motionFinished=*moved.value;
    }
    if(!pending.motionFinished) return DomainStatus::Applied;
    if(!pending.directSkill) {
        auto pulse=std::make_shared<effects::NativeAreaPulse>();auto &spec=pulse->spec;
        spec.source=actor.actor;spec.area=actor.area;spec.skill=pending.skill.sourceId;spec.rank=pending.skill.rank;
        spec.center=landing;spec.radius=formula("calc3");spec.filter=0x8583;
        const auto &row=rules.formulas->skills.at(pending.skill.sourceId);
        spec.hitFlags=uint32_t(row.number("hitflags"));spec.resultFlags=uint16_t(row.number("resultflags"))|1;
        if(pending.skill.sourceId==143) {
            spec.resultFlags|=4;
            auto attack=pending.skill;attack.effect=SkillBehavior::WeaponProjectile;attack.leap.reset();attack.weapon.emplace();
            attack.weapon->damagePercent=formula("calc1");attack.hitClass=3;
            auto contact=std::make_shared<combat::SpellImpact>();contact->source=contact->projectile=actor.actor;
            contact->weapon=prepareWeaponSkillProcedures(owner.totals.equipment.weapons[0],owner.totals.character.combat,attack,owner.persistent.player.level,false);
            contact->hitClass=3;spec.missileContact=std::move(contact);spec.directFunction=153;
        } else spec.damage.hitClass=3;
        pending.directSkill=std::move(pulse);
    }
    const auto removed=ports_.effects.removeState(actor,pending.skill.leap->moving.id);if(!removed) return removed.status;
    pending.nativeCallbackStarted=true;
    const auto hit=ports_.effects.nativeArea(pending.directSkill->spec,actor.tick,pending.directSkill);
    if(!hit) return hit.status;
    pending.landed=true;pending.nativeCallbackStarted=false;state_.casts.at(actor.actor).until=actor.tick+4;
    return DomainStatus::Applied;
}
}
