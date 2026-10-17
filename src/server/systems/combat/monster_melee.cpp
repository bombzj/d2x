#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/combat/weapon_snapshot.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x::server::combat {
DomainResult<> System::monsterMelee(EntityId source,EntityId target,const MonsterAttackRule &program,uint64_t occurrence,TickContext tick) {
    const auto existing=std::find_if(state_.spells.begin(),state_.spells.end(),[&](const auto &impact) {
        return impact.source==source && impact.occurrence==occurrence;
    });
    if(existing==state_.spells.end()) {
        const auto *unit=ports_.monsters.find(source);
        const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source);
        if(!unit || !unit->alive() || !caster || !caster->programs()) return {DomainStatus::InvalidActor,{}};
        const auto &rules=*caster->programs();
        const Participants participants{ports_.players,ports_.monsters,ports_.social};
        if(!participants.find(target).aliveIn(unit->area) || !participants.canHarm(source,target)) return {DomainStatus::Unavailable,{}};
        const auto stats=ports_.effects.unitStatValues(source,tick.tick,
            unit->petWeapon && unit->petWeapon->item?std::optional{unit->petWeapon->item}:std::nullopt,program.modeStats);
        if(!stats) return {DomainStatus::Unavailable,{}};
        auto modifiers=ports_.effects.combatModifiers(source,tick.tick);
        auto weapon=sampleNativeWeapon(unit->petWeapon.value_or(WeaponDamage{}),*stats,*rules.stats,modifiers,true);
        if(unit->rule.enchantment) {
            weapon.statDamagePercent=nativeAdd(weapon.statDamagePercent,unit->rule.enchantment->damagePercent);
            weapon.damagePercent=nativeAdd(weapon.damagePercent,unit->rule.enchantment->damagePercent);
            weapon.attackRatingPercent=nativeAdd(weapon.attackRatingPercent,unit->rule.enchantment->attackRatingPercent);
        }
        weapon.hitClass=program.hitClass>=0?program.hitClass:unit->rule.hitClass;
        SkillCastSpec skill=program.weaponSkill.value_or(SkillCastSpec{});
        if(!skill.weapon) skill.weapon=WeaponSkillSpec{};
        if(!program.weaponSkill) {
            skill.sourceId=0;skill.rank=1;
        }
        skill.weapon->knockback=skill.weapon->knockback || program.knockback || logicalStatValue(*stats,*rules.stats,"item_knockback")!=0;
        SpellImpact impact;impact.projectile=impact.source=source;impact.area=unit->area;impact.targets={target};
        impact.occurrence=occurrence;impact.melee=impact.nativeDamageFlagsOnly=impact.synchronous=true;
        impact.binding=participants.bind(participants.find(source));
        impact.weapon=prepareWeaponSkillProcedures(weapon,modifiers,skill,unit->rule.level,false);
        impact.weapon->automatic=program.alwaysHit;
        impact.nativeSkill=std::make_shared<NativeSkillContact>();
        impact.nativeSkill->melee=true;impact.nativeSkill->skill=skill.sourceId;impact.nativeSkill->rank=skill.rank;
        impact.nativeSkill->sourceDamage=128;impact.nativeSkill->monsterProgram=program;
        impact.missileDamage=std::make_shared<MissileDamageRollState>();
        auto &damage=*impact.missileDamage;damage.formulas=rules.formulas;damage.statRules=rules.stats;
        damage.uninterruptableState=rules.uninterruptable.id;
        damage.lifeStealDivisor=rules.lifeStealDivisor;damage.manaStealDivisor=rules.manaStealDivisor;
        damage.lifeStealOverlay=rules.lifeStealOverlay;damage.manaStealOverlay=rules.manaStealOverlay;
        impact.areaDamage=MissileAreaDamage{};impact.areaDamage->resultFlags=5;impact.areaDamage->hitFlags=0x20;
        // Do109 starts a zeroed DamageStrc, then ORs Skills.HitFlags on
        // success; it does not inherit the ordinary monster attack flag.
        if(skill.weapon->doFunction==109) impact.areaDamage->hitFlags=0;
        impact.areaDamage->rolled=std::make_shared<MissileAreaRoll>();impact.monsterStates=unit->rule.hitStates;
        auto prepared=prepareSpells({std::move(impact)});if(!prepared) return {prepared.status,{}};
        commitSpells(std::move(*prepared.value));
    }
    if(resolveSpells(tick,std::pair{source,occurrence})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
    return {DomainStatus::Applied,std::monostate{}};
}
}
