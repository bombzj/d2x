#include "progressive.hpp"
#include "../system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/effects/system.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::combat {
DomainResult<std::vector<missiles::NativeSpawn>> preparePd2ProgressiveBirths(const Ports &ports,const SpellImpact &impact,
    const AssassinCharge &charge,AssassinProgressiveStep step,ParticipantView target,TickContext tick,uint64_t &random) {
    const auto caster=skills::nativeCaster(ports.players,ports.monsters,impact.source);
    if(!caster || !caster->programs() || !charge.program) return {DomainStatus::Stale,{}};
    const auto &rules=*caster->programs();const auto actor=caster->context(ports.areas,tick.tick);
    auto input=caster->formulaInput(ports.effects,tick.tick);SkillFormulaValues values(*rules.formulas,input);
    const int missile=charge.program->missiles.at(size_t(step.charge-1));
    if(missile<0) return {DomainStatus::Unavailable,{}};
    const auto origin=caster->position(),point=target.position();
    std::vector<missiles::NativeSpawn> result;
    const auto make=[&](Vec start,Vec destination) {
        return missiles::NativeSpawn{caster->id(),actor.area,actor.areaGeneration,tick.tick,start,destination,missile,charge.skill,charge.rank};
    };
    if(step.function==139) {
        auto spawn=make(origin,point);spawn.copyMonsterAttackBonus=true;
        spawn.finalParameters=std::array<int32_t,2>{int32_t(origin.y)-int32_t(point.y),int32_t(point.x)-int32_t(origin.x)};
        result.push_back(std::move(spawn));return {DomainStatus::Applied,std::move(result)};
    }
    const auto count=values.calculation(FormulaDomain::Skill,charge.skill,charge.rank,"prgcalc"+std::to_string(step.charge));
    if(!count) throw std::logic_error("Missing PD2 progressive missile count");
    if(*count<0) return {DomainStatus::Unavailable,{}};
    if(*count>512) return {DomainStatus::Capacity,{}};
    missiles::NativeSpawn::DamageAdds adds;
    if(charge.program->chargeStat==319) {
        const auto stats=ports.effects.unitStatValues(caster->id(),tick.tick);
        if(!stats || !impact.weapon) return {DomainStatus::Unavailable,{}};
        SkillCastSpec attack;attack.effect=SkillBehavior::WeaponProjectile;attack.sourceId=charge.skill;attack.rank=charge.rank;attack.weapon.emplace();
        const auto modifiers=caster->player?caster->player->totals.character.combat:ports.effects.combatModifiers(caster->id(),tick.tick);
        const auto &row=rules.formulas->skills.at(charge.skill);
        // Do171/172 call FillDamageValues afresh, then normalize the owner's
        // equipment poison to this skill's elemental length in single precision.
        // Keep the physical and other elemental rolls in their native order.
        (void)rollNativeWeaponPhysical(impact.weapon->weapon,modifiers,attack,caster->level(),
            target.monster && target.monster->rule.demon,target.monster && target.monster->rule.undead,random,{row.number("srcdam"),0});
        MissileHitDamage damage;fillNativeWeaponElements(damage,*stats,*rules.stats,row.number("srcdam"),random,caster->monster && !caster->monster->hireling);
        const auto length=values.parameter(FormulaDomain::Skill,charge.skill,charge.rank,"edln");
        if(!length) throw std::logic_error("Missing PD2 Cobra poison normalization length");
        int32_t amount=damage.channels[5];
        if(*length) {
            const float normalized=float(uint32_t(damage.channels[5]))*(float(uint32_t(damage.poisonLength))/float(*length));
            const uint32_t narrowed=normalized<0?0:double(normalized)>=4294967296.0?UINT32_MAX:uint32_t(std::trunc(normalized));
            amount=nativeNarrow(narrowed);
        }
        adds.minimumPoison=adds.maximumPoison=amount;
    }
    if(step.function==171) {
        const auto layout=pd2MissileFanLayout(origin,point,*count);
        for(int index=0;index<std::max(1,*count);++index) {
            auto spawn=make(origin,layout.first+layout.step*float(index));
            spawn.activateFrame=1;spawn.suppressSourceEvents=true;spawn.damageAdds=adds;
            result.push_back(std::move(spawn));
        }
    } else if(step.function==172) {
        const int countRays=!*count || *count>64?64:*count;
        for(int direction=0;direction<64;direction+=64/countRays) {
            auto spawn=make(point,point+missileRingDirection(direction));spawn.damageAdds=adds;
            result.push_back(std::move(spawn));
        }
    } else return {DomainStatus::NotImplemented,{}};
    return {DomainStatus::Applied,std::move(result)};
}
}
