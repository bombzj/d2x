#include "system.hpp"
#include "server/systems/actions/release.hpp"
#include "native_caster.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/companions/system.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace d2x::server::skills {
DomainResult<bool> System::startArctic(Release &pending,const ActorContext &actor,std::span<const DomainFact> facts) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return {DomainStatus::InvalidActor,{}};
    const auto &rules=*caster->programs();const auto state=rules.formulas->infernoState;
    const auto *states=ports_.effects.unitEffects(actor.actor);const bool warm=states && states->findState(state);
    if(caster->player && caster->player->persistent.player.mana<(warm?pending.skill.manaCost:std::max(pending.skill.manaCost,pending.skill.startMana))) return {DomainStatus::Unavailable,{}};
    auto skill=pending.skill;CombatEffectSpec effect;
    effect.state=caster->player?caster->player->rules.character->states->at(state):caster->monster->rule.states->at(state);
    effect.source={CombatEffectSource::Skill,actor.actor,skill.sourceId,skill.rank};effect.duration=warm?6:20;
    effect.nativeState=effect.nativeDirectAllocation=true;effect.nativeValues.emplace();effect.stacking=EffectStacking::ReuseState;
    effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;skill.appliedEffect=std::move(effect);skill.nativeEffectWrites.clear();
    const auto installed=caster->monster?ports_.effects.unitSkill(actor.actor,skill,actor.tick):ports_.effects.skill(actor,skill,{},facts);
    if(!installed) return {installed.status,{}};
    if(!warm) state_.skillParameter(actor.actor,{pending.skill.sourceId,pending.skill.charge?pending.skill.charge->item.id:EntityId{}},0,0);
    pending.manaPaid=true;
    if(pending.skill.charge && caster->player) pending.skill.charge->item=caster->player->persistent.inventory.items.at(pending.skill.charge->item.id).handle();
    return {DomainStatus::Applied,warm};
}
DomainStatus System::activateDruid(Release &pending,const ActorContext &actor,Vec target) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !pending.skill.druid) return DomainStatus::InvalidActor;
    const auto &rules=*caster->programs();const auto &program=*pending.skill.druid;
    if(program.program==DruidProgram::Raven || program.program==DruidProgram::Vine || program.program==DruidProgram::Summon) {
        auto skill=pending.skill;if(pending.manaPaid || caster->monster) {skill.manaCost=0;skill.charge.reset();}
        return ports_.companions.druid(actor,skill,target).status;
    }
    if(program.program==DruidProgram::Periodic) return startDruidPeriodic(pending,actor);
    if(program.program==DruidProgram::Shape) {
        const auto removed=ports_.effects.removeStateGroup(actor,program.state.group);if(!removed) return removed.status;
        if(*removed.value) return DomainStatus::Applied;
        auto skill=pending.skill;if(pending.manaPaid || caster->monster) {skill.manaCost=0;skill.charge.reset();}
        return (caster->monster?ports_.effects.unitSkill(actor.actor,skill,actor.tick):ports_.effects.skill(actor,skill)).status;
    }
    auto input=caster->formulaInput(ports_.effects,actor.tick);SkillFormulaValues values(*rules.formulas,input);
    const auto formula=[&](std::string_view field) {
        const auto value=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,field);
        if(!value) throw std::runtime_error("Unavailable native druid formula");
        return *value;
    };
    if(program.program==DruidProgram::Arctic) {
        const NativeSkillSlot slot{pending.skill.sourceId,pending.skill.charge?pending.skill.charge->item.id:EntityId{}};
        if(!state_.skillParameter(actor.actor,slot,0)) {state_.skillParameter(actor.actor,slot,0,1);return DomainStatus::Applied;}
        missiles::NativeSpawn spawn{actor.actor,actor.area,actor.areaGeneration,actor.tick,caster->position(),target,pending.skill.missileId,pending.skill.sourceId,pending.skill.rank};
        spawn.totalFrames=std::max(formula("calc1"),1);
        return ports_.missiles.spawnNative(spawn).status;
    }
    std::vector<missiles::NativeSpawn> births;
    const auto append=[&](Vec origin,Vec destination,int missile) -> missiles::NativeSpawn & {
        births.push_back({actor.actor,actor.area,actor.areaGeneration,actor.tick,origin,destination,
            missile,pending.skill.sourceId,pending.skill.rank});return births.back();
    };
    switch(program.program) {
    case DruidProgram::Firestorm:case DruidProgram::Wind: {
        const int count=formula("calc1");if(count<=0) return DomainStatus::Unavailable;
        if(count>512) return DomainStatus::Capacity;
        for(int index=0;index<count;++index) {
            auto &spawn=append(caster->position(),target,pending.skill.missileId);
            if(program.program==DruidProgram::Firestorm && !index) spawn.copyMonsterAttackBonus=true;
            else {spawn.initialization=missiles::NativeSpawn::Initialization::ChargedSkill;
                spawn.initializationIndex=index-(program.program==DruidProgram::Firestorm?1:0);}
        }
        break;
    }
    case DruidProgram::Boulder:
        append(caster->position(),target,pending.skill.missileId).copyMonsterAttackBonus=true;break;
    case DruidProgram::Fissure:case DruidProgram::Volcano: {
        const auto missile=rules.formulas->missiles.find(pending.skill.missileId);
        if(missile==rules.formulas->missiles.end()) return DomainStatus::Unavailable;
        const int size=missile->second.number("size");
        const auto &area=ports_.areas.at(actor.area);
        if(!int(target.x+area.definition.origin.x) || !int(target.y+area.definition.origin.y) ||
           !area.definition.collision.missileSegment(target,target,{5,size})) return DomainStatus::Unavailable;
        auto &spawn=append(target,target,pending.skill.missileId);
        if(program.program==DruidProgram::Volcano) {
            auto random=caster->random();const auto rolled=rollRandom(random);
            spawn.ownerSeedEdit=std::pair{caster->random(),random};spawn.finalParameters=std::array{nativeNarrow(rolled),0};
        } else spawn.copyMonsterAttackBonus=true;
        break;
    }
    case DruidProgram::ShockWave: {
        const int count=formula("calc1");if(count<=0) return DomainStatus::Unavailable;
        if(count>512) return DomainStatus::Capacity;
        for(const auto destination:missileFanTargets(caster->position(),target,count,{0,1}))
            append(caster->position(),destination,pending.skill.missileId);
        break;
    }
    default:return DomainStatus::NotImplemented;
    }
    std::vector<DomainFact> extra;
    if(program.program==DruidProgram::Volcano && !births.empty() && births.front().finalParameters)
        extra.emplace_back(SkillPulseFact{actor.actor,{},uint8_t(caster->monster?1:0),1,actor.area,pending.skill.sourceId,pending.skill.rank,target,uint8_t((*births.front().finalParameters)[0]),uint32_t(int(target.x+ports_.areas.at(actor.area).definition.origin.x)),uint32_t(int(target.y+ports_.areas.at(actor.area).definition.origin.y))});
    auto prepared=ports_.missiles.prepareNativeBirth(births,0,extra);if(!prepared) return prepared.status;
    if(caster->monster) return ports_.missiles.commitNativeBirth(std::move(*prepared.value)).status;
    const auto *player=caster->player;
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
    if(!pending.manaPaid) {edit.player.mana-=pending.skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{pending.skill.manaCost,pending.skill.sourceId};edit.charge=pending.skill.charge;}
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return plan.status;
    plan.value->missileBirth=std::move(*prepared.value);
    return ports_.transactions.commit(std::move(*plan.value)).status;
}
}
