#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "gameplay/combat/weapon_snapshot.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/druid_spec.hpp"

namespace d2x::server::missiles {
DomainResult<DirectContact> System::nativeWeaponContact(const Spawn &request,std::vector<EntityId> targets) {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,request.actor.actor);
    if(!caster || !caster->programs()) return {DomainStatus::InvalidActor,{}};
    const auto &rules=*caster->programs();
    if(targets.size()!=1 || !request.weapon || !request.skill.weapon) return {DomainStatus::InvalidRequest,{}};
    combat::SpellImpact hit;hit.projectile=hit.source=caster->id();hit.area=request.directArea.value_or(request.actor.area);hit.nativeAura=request.directArea.has_value();
    hit.occurrence=request.nativeMeleeOccurrence.value_or(request.actor.tick);hit.targets=std::move(targets);hit.melee=true;hit.nativeDamageFlagsOnly=true;hit.synchronous=true;
    auto weapon=request.weapon->weapon;
    auto modifiers=caster->player?caster->player->totals.character.combat:ports_.effects.combatModifiers(caster->id(),request.actor.tick);
    if(caster->monster) {
        const auto stats=ports_.effects.unitStatValues(caster->id(),request.actor.tick,weapon.item?std::optional{weapon.item}:std::nullopt,
            request.nativeMeleeContact && request.nativeMeleeContact->monsterProgram?request.nativeMeleeContact->monsterProgram->modeStats:nullptr);
        if(!stats) return {DomainStatus::Unavailable,{}};
        weapon=sampleNativeWeapon(weapon,*stats,*rules.stats,modifiers,true);
    }
    hit.weapon=prepareWeaponSkillProcedures(weapon,modifiers,request.skill,request.weapon->level,false);
    hit.nativeSkill=request.nativeMeleeContact?request.nativeMeleeContact:std::make_shared<combat::NativeSkillContact>();hit.nativeSkill->skill=request.skill.sourceId;hit.nativeSkill->rank=request.skill.rank;
    hit.nativeSkill->modeRevision=caster->player?request.actor.sequence:
        caster->monster->nativeActionRevision+(request.nativeMeleeHeld?1:0);
    hit.nativeDrainHeld=request.nativeMeleeHeld;
    hit.nativeSkill->melee=true;hit.nativeSkill->progressive=hit.nativeSkill->kick || request.skill.weapon->commonAttack ||
        (request.skill.assassin && request.skill.assassin->program==AssassinProgram::DragonClaw);
    hit.nativeSkill->initialDamage=request.nativeMeleeBase;
    // SrvDo001 fills ordinary Attack with 128, independently of Skills.srcdam.
    if(request.skill.weapon->commonAttack) hit.nativeSkill->sourceDamage=128;
    hit.nativeSkill->skipMissDrain=request.skill.weapon->doFunction==78 || (request.skill.assassin && request.skill.assassin->program==AssassinProgram::BladeShield) ||
        (request.skill.druid && (request.skill.druid->program==DruidProgram::FireClaws || request.skill.druid->program==DruidProgram::Hunger));
    hit.retainDeadSource=request.deathTrigger;
    hit.missileDamage=std::make_shared<MissileDamageRollState>();hit.missileDamage->formulas=rules.formulas;hit.missileDamage->statRules=rules.stats;
    hit.missileDamage->uninterruptableState=rules.uninterruptable.id;
    hit.missileDamage->lifeStealDivisor=rules.lifeStealDivisor;hit.missileDamage->manaStealDivisor=rules.manaStealDivisor;
    hit.missileDamage->lifeStealOverlay=rules.lifeStealOverlay;hit.missileDamage->manaStealOverlay=rules.manaStealOverlay;
    hit.areaDamage=combat::MissileAreaDamage{};hit.areaDamage->resultFlags=5;hit.areaDamage->hitFlags=0x20;
    hit.areaDamage->rolled=std::make_shared<combat::MissileAreaRoll>();
    hit.monsterStates={rules.chilled,rules.poisoned,{}};
    auto prepared=ports_.combat.prepareSpells({std::move(hit)});if(!prepared) return {prepared.status,{}};
    auto cost=request.cost;
    if(!cost && !request.free) {
        if(!caster->player) return {DomainStatus::InvalidActor,{}};
        const auto &player=*caster->player;
        transactions::CharacterEdit edit{request.actor,player.inventoryRevision,player.characterRevision,player.persistent.player};
        edit.player.mana-=request.skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{request.skill.manaCost,request.skill.sourceId};edit.charge=request.skill.charge;
        auto transaction=ports_.transactions.prepare(std::move(edit));if(!transaction) return {transaction.status,{}};
        cost=std::move(*transaction.value);
    }
    if(cost) {const auto result=ports_.transactions.commit(std::move(*cost));if(!result) return {result.status,{}};}
    ports_.combat.commitSpells(std::move(*prepared.value));return {DomainStatus::Applied,DirectContact{caster->id(),request.nativeMeleeOccurrence.value_or(request.actor.tick)}};
}
}
