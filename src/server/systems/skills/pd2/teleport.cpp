#include "../system.hpp"
#include "../runtime.hpp"
#include "../evaluation.hpp"
#include "../native_caster.hpp"
#include "execution.hpp"
#include "requirements.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/effects/native_area.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2/arithmetic.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace d2x::server::skills {
DomainStatus System::activatePd2MonsterTeleport(Release &pending,const ActorContext &actor,Vec target) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->monster || !caster->programs() || !pending.skill.pd2) return DomainStatus::InvalidActor;
    const auto &program=*pending.skill.pd2;
    // Current monster callers 357/573 have no mastery debuff. Do155 owns
    // the movement and optional landing ring, independently of player travel.
    if(program.doFunction!=155 || program.state.id>=0) return DomainStatus::NotImplemented;
    if(!pending.manaPaid) {
        const auto *states=ports_.effects.unitEffects(actor.actor);
        if(states && states->hasState(214,actor.tick)) return DomainStatus::Unavailable;
        const auto adjusted=ports_.travel.pd2TeleportLanding(actor.area,caster->position(),target,caster->monster->rule.size);
        if(!adjusted) return adjusted.status;
        const auto &area=ports_.areas.at(actor.area).definition;
        const auto global=*adjusted.value+area.origin;
        const auto room=ports_.areas.roomAtPosition(actor.area,caster->position(),int(std::floor(global.x+.5f)),int(std::floor(global.y+.5f)));
        if(!room) return DomainStatus::Unavailable;
        const auto landing=ports_.areas.nativeFreeCoordinates(*room,int(std::floor(global.x+.5f)),int(std::floor(global.y+.5f)),
            caster->monster->rule.size,0x1c09,ports_.spatial.nativeCollisionSample(0x1c09));
        if(!landing) return DomainStatus::Unavailable;
        if(landing->room.area!=actor.area) return DomainStatus::NotImplemented;
        const auto moved=ports_.monsters.teleport(actor.actor,landing->position,actor.tick,0,true);
        if(!moved) return moved.status;
        pending.manaPaid=true;
    }
    if(program.missiles[0]>=0 && !pending.pulses) {
        if(!pending.pd2Execution) pending.pd2Execution=std::make_shared<Pd2CastExecution>();
        auto &birth=pending.pd2Execution->teleportMissiles;
        pending.nativeCallbackStarted=true;
        if(!birth) {
            const auto current=nativeCaster(ports_.players,ports_.monsters,actor.actor);
            if(!current) return DomainStatus::Stale;
            auto input=current->formulaInput(ports_.effects,actor.tick);
            SkillFormulaValues values(*current->programs()->formulas,input);
            const auto count=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc2");
            if(!count) throw std::logic_error("Missing PD2 Blink landing ring formula");
            if(*count>0) {
                const int step=64/std::min(*count,64);
                std::vector<missiles::NativeSpawn> ring;
                for(int direction=0;direction<64;direction+=step)
                    ring.push_back({actor.actor,actor.area,actor.areaGeneration,actor.tick,current->position(),
                        current->position()+missileRingDirection(direction),program.missiles[0],pending.skill.sourceId,pending.skill.rank});
                auto prepared=ports_.missiles.prepareNativeBirth(ring);
                if(!prepared) return prepared.status;
                birth=std::move(*prepared.value);
            } else pending.pulses=1;
        }
        if(birth) {
            const auto committed=ports_.missiles.commitNativeBirth(birth);
            if(!committed) return committed.status;
            pending.pulses=1;
        }
    }
    pending.nativeCallbackStarted=false;
    return DomainStatus::Applied;
}
DomainStatus System::activatePd2Teleport(Release &pending,const ActorContext &actor,Vec target) {
    const auto &program=*pending.skill.pd2;
    if(program.doFunction!=155 && program.doFunction!=156 && program.doFunction!=157) return DomainStatus::NotImplemented;
    if(program.missiles[0]>=0 && program.doFunction!=156) return DomainStatus::NotImplemented;
    if(!pending.manaPaid) {
        // Current 36B570 checks STATE_DISABLE_TELE (214) at release,
        // including Gust and Blood Warp. A start-time check is insufficient.
        const auto *states=ports_.effects.unitEffects(actor.actor);
        if(states && states->hasState(214,actor.tick)) return DomainStatus::Unavailable;
        const auto moved=ports_.travel.castTeleport(actor,{actor.area,actor.areaGeneration,target},pending.skill.manaCost,pending.skill.charge,pending.skill.sourceId);
        if(!moved) return moved.status;
        pending.manaPaid=true;
    }
    const auto &owner=*ports_.players.find(actor.player);
    if(program.doFunction==156) {
        // Current Do156, RVA 0x3AF4F0: the zeroed damage record receives
        // Calc1 as stun length and Calc3 as radius after the teleport. The
        // separate SKILLS_CreateMissile call creates the landing effect.
        if(!pending.directSkill) {
            auto input=formulaInput(owner);SkillFormulaValues values(*owner.rules.skills->formulas,input);
            const auto frames=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc1");
            const auto radius=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc3");
            if(!frames || !radius) throw std::logic_error("Missing PD2 Gust area formulas");
            auto pulse=std::make_shared<effects::NativeAreaPulse>();auto &area=pulse->spec;
            const auto &row=owner.rules.skills->formulas->skills.at(pending.skill.sourceId);
            area.source=actor.actor;area.area=actor.area;area.center=owner.position;
            area.skill=pending.skill.sourceId;area.rank=pending.skill.rank;area.radius=*radius;area.filter=0x8583;
            area.hitFlags=uint32_t(row.number("hitflags"));area.resultFlags=uint16_t(row.number("resultflags"));
            area.damage.stunLength=*frames;area.damage.hitClass=row.number("hitclass");
            if(program.missiles[0]>=0) {
                missiles::NativeSpawn spawn{actor.actor,actor.area,actor.areaGeneration,actor.tick,owner.position,{},
                    program.missiles[0],pending.skill.sourceId,pending.skill.rank};
                spawn.copyMonsterAttackBonus=true;
                const std::array births{spawn};
                auto birth=ports_.missiles.prepareNativeBirth(births);if(!birth) return birth.status;
                const auto born=ports_.missiles.commitNativeBirth(std::move(*birth.value));if(!born) return born.status;
            }
            pending.directSkill=std::move(pulse);
        }
        pending.nativeCallbackStarted=true;
        const auto result=ports_.effects.nativeArea(pending.directSkill->spec,actor.tick,pending.directSkill);
        if(result) pending.nativeCallbackStarted=false;
        return result.status;
    }
    if(program.doFunction==157 && !pending.skill.appliedEffect) {
        const auto cost=pd2BloodWarpCost(owner,pending.skill);
        if(cost>=owner.persistent.player.hp.raw()) return DomainStatus::Unavailable;
        auto input=formulaInput(owner);SkillFormulaValues values(*owner.rules.skills->formulas,input);
        const auto duration=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"auralencalc");
        if(!duration || *duration<=0 || program.state.id<0) return DomainStatus::Unavailable;
        CombatEffectSpec effect;effect.state=program.state;effect.duration=EffectFrame(*duration);
        effect.nativeState=effect.nativeDirectAllocation=true;effect.nativeValues.emplace();
        effect.source={CombatEffectSource::Skill,actor.actor,pending.skill.sourceId,pending.skill.rank};
        const auto key=StatKey{owner.rules.skills->stats->names.at("hpregen"),0};
        const auto regeneration=nativeAdd(nativeDivide(cost,*duration),1);
        effect.nativeValues->emplace(key,regeneration);
        transactions::CharacterEdit payment{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};
        payment.player.hp=Fixed8::fromRaw(nativeSubtract(payment.player.hp.raw(),cost));
        auto plan=ports_.transactions.prepare(std::move(payment));if(!plan) return plan.status;
        const auto paid=ports_.transactions.commit(std::move(*plan.value));if(!paid) return paid.status;
        pending.skill.appliedEffect=std::move(effect);pending.skill.nativeEffectWrites.emplace_back(key,regeneration);
    }
    if(!pending.skill.appliedEffect && program.state.id>=0) {
        auto input=formulaInput(owner);SkillFormulaValues values(*owner.rules.skills->formulas,input);
        const auto reduction=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc1");
        const auto duration=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"auralencalc");
        if(!reduction || !duration) throw std::runtime_error("Missing PD2 Teleport debuff formula");
        const auto factor=nativeSubtract(owner.rules.skills->formulas->skills.at(pending.skill.sourceId).number("param2"),*reduction);
        CombatEffectSpec effect;effect.state=program.state;effect.duration=EffectFrame(uint32_t(*duration));
        effect.nativeState=true;effect.stacking=EffectStacking::SkillLevel;effect.nativeValues.emplace();
        effect.source={CombatEffectSource::Skill,actor.actor,pending.skill.sourceId,pending.skill.rank};
        const auto &stats=*owner.rules.skills->stats;
        for(const auto name:{"passive_fire_mastery","passive_cold_mastery","passive_ltng_mastery","passive_pois_mastery"}) {
            const auto value=logicalStatValue(owner.totals.nativeStats,stats,name);
            const int adjusted=nativeSubtract(0,nativeAdd(pd2MultiplyDivide(value,factor,100),factor));
            const auto key=StatKey{stats.names.at(name),0};
            effect.nativeValues->emplace(key,adjusted);pending.skill.nativeEffectWrites.emplace_back(key,adjusted);
        }
        effect.modifiers=nativeCharacterModifiers(StatSnapshot{*effect.nativeValues},stats,{});
        pending.skill.appliedEffect=std::move(effect);
    }
    if(!pending.skill.appliedEffect) return DomainStatus::Applied;
    auto debuff=pending.skill;debuff.manaCost=debuff.startMana=0;debuff.charge.reset();
    pending.nativeCallbackStarted=true;
    const auto applied=ports_.effects.skill(actor,debuff);
    if(applied) pending.nativeCallbackStarted=false;
    return applied.status;
}
}
