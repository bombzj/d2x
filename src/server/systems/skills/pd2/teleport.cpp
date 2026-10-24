#include "../system.hpp"
#include "../runtime.hpp"
#include "../evaluation.hpp"
#include "requirements.hpp"
#include "server/player_store.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/effects/native_area.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x::server::skills {
namespace {
int scaledMastery(int value,int reduction) {
    // Current core RVA 0x37A6F0: multiply in double, divide, saturate high.
    const double scaled=double(value)*double(reduction)/100.0;
    if(scaled>double(INT32_MAX)) return INT32_MAX;
    if(scaled<double(INT32_MIN)) return INT32_MIN;
    return int(std::trunc(scaled));
}
}
DomainStatus System::activatePd2Teleport(Release &pending,const ActorContext &actor,Vec target) {
    const auto &program=*pending.skill.pd2;
    if(program.doFunction!=155 && program.doFunction!=156 && program.doFunction!=157) return DomainStatus::NotImplemented;
    if(program.missiles[0]>=0 && program.doFunction!=156) return DomainStatus::NotImplemented;
    if(!pending.manaPaid) {
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
            const int adjusted=nativeSubtract(0,nativeAdd(scaledMastery(value,factor),factor));
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
