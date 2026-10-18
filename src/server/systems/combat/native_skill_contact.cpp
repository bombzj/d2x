#include "system.hpp"
#include "player_contact.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/actions/state.hpp"
#include "server/systems/reactions/item_triggers.hpp"
#include "server/systems/inventory/system.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/combat/weapon_block.hpp"
#include "gameplay/combat/target_bonus.hpp"
#include "gameplay/monsters/enchantment_damage.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/weapon_snapshot.hpp"
#include "gameplay/combat/alignment.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::combat {
DomainResult<bool> System::directDefense(SpellImpact &impact,ParticipantView source,ParticipantView target,TickContext tick) {
    auto &cursor=impact.directDefense;if(cursor.target!=target.id()) cursor={target.id()};
    const auto consume=[&](ParticipantView unit,uint64_t expected,uint64_t next) {
        return unit.player?ports_.transactions.consumeRandom({unit.player->player,unit.id(),unit.player->area,
            ports_.areas.at(unit.player->area).generation,0,tick.tick},expected,next):ports_.monsters.consumeRandom(unit.id(),expected,next);
    };
    if(!cursor.accuracyPrepared) {
        if(impact.monsterToHit && source.id()) {
            const auto expected=source.player?source.player->combatRandom:source.monster->combatRandom;auto random=expected;
            const bool running=target.player && target.player->moving && target.player->runningNow;
            cursor.hit=running || int(limitedRandom(random,100))<physicalHitChance(impact.monsterLevel,impact.monsterRating,
                target.player?target.player->persistent.player.level:target.monster->rule.level,
                target.player?target.player->totals.character.defense:ports_.effects.unitDefense(target.id(),tick.tick));
            const auto accepted=consume(source,expected,random);if(!accepted) return {accepted.status,{}};
        }
        cursor.accuracyPrepared=true;
    }
    if(cursor.hit && !cursor.prepared && !impact.unblockable) {
        const auto stats=target.player?std::optional{target.player->totals.nativeStats}:ports_.effects.unitStatValues(target.id(),tick.tick);
        if(!stats) return {DomainStatus::Unavailable,{}};
        const auto &rules=target.player?*target.player->rules.skills->stats:*target.monster->rule.stats;
        const auto *gear=target.player?&target.player->totals.equipment:target.monster->equipmentStats?&*target.monster->equipmentStats:nullptr;
        int block=target.player && target.player->totals.equipment.shield?target.player->totals.equipment.blockChance:
            target.monster && (target.monster->shield || target.monster->rule.blockWithoutShield || target.monster->rule.nativeBlockException)?
                std::min(75,stats->get({rules.names.at("toblock"),0})):0;
        if(target.player && target.player->moving && target.player->runningNow) block/=3;
        const auto expected=target.player?target.player->combatRandom:target.monster->combatRandom;auto random=expected;
        const auto mods=target.player?target.player->totals.character.combat:ports_.effects.combatModifiers(target.id(),tick.tick);
        const bool physical=impact.monsterHit?impact.monsterHit->channels[0]>0:impact.type==DamageType::Physical && impact.currentDamage()>0;
        const auto result=rollNativeBlockOrDodge(block,physical,target.moving(),gear?nativeWeaponBlockChance(*gear,*stats,rules):0,
            gear && gear->animationClass=="ht2",mods,!impact.melee,random);
        const auto accepted=consume(target,expected,random);if(!accepted) return {accepted.status,{}};
        cursor.result=result;cursor.prepared=true;
    }
    if(!cursor.reactionComplete) {
        DomainResult<> result{DomainStatus::Applied,std::monostate{}};
        if(cursor.result.block) result=target.monster?ports_.monsters.block(target.id(),tick.tick):
            ports_.effects.block({target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick});
        else if(target.player && cursor.result.avoidance!=WeaponAvoidance::None)
            result=ports_.effects.avoidance({target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick},cursor.result.avoidance,impact.source);
        if(!result) return {result.status,{}};
        cursor.reactionComplete=true;
    }
    return {DomainStatus::Applied,cursor.hit && !cursor.result.block && cursor.result.avoidance==WeaponAvoidance::None};
}
DomainStatus System::postNativeMeleeAllocation(SpellImpact &impact,TickContext tick) {
    if(!impact.nativeSkill || !impact.nativeSkill->melee || impact.nativeSkill->postAllocationComplete) return DomainStatus::Applied;
    auto &execution=*impact.nativeSkill;
    const auto &skill=impact.weapon->wearSkill;
    if(skill.weapon->doFunction==70 || (execution.monsterProgram && skill.weapon->startFunction==32)) {
        const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,impact.source);if(!caster) return DomainStatus::Stale;
        auto input=caster->formulaInput(ports_.effects,tick.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
        const auto flat=values.calculation(FormulaDomain::Skill,skill.sourceId,skill.rank,"calc2");
        if(!flat) throw std::logic_error("Missing native melee post-allocation formula");
        const auto amount=nativeShiftLeft(*flat,8);
        if(impact.playerContact) {
            auto &physical=impact.playerContact->defense.damage.channels[0].dealt;physical=nativeAdd(nativeChecked(physical),amount);
        } else if(impact.monsterContact) {
            auto &physical=impact.monsterContact->physical;physical=nativeAdd(nativeChecked(physical),amount);
        }
    }
    execution.postAllocationComplete=true;return DomainStatus::Applied;
}
bool System::addAllocatedPhysical(EntityId source,uint64_t occurrence,int32_t amount) {
    for(auto &impact:state_.spells) if(impact.source==source && impact.occurrence==occurrence && impact.nativeDrainHeld && impact.nativeSkill && impact.nativeSkill->heldPrepared) {
        if(impact.playerContact) {
            auto &damage=impact.playerContact->defense.damage;
            damage.channels[0].dealt=nativeAdd(nativeChecked(damage.channels[0].dealt),amount);
        } else if(impact.monsterContact) {
            auto &contact=*impact.monsterContact;contact.physical=nativeAdd(nativeChecked(contact.physical),amount);
        } else return false;
        // St32 mutates dwPhysDamage after CalculateTotalDamage. ExecuteEvents
        // only recalculates totals for missiles: preserve the cached HP damage.
        impact.nativeSkill->allocatedPhysical=nativeAdd(impact.nativeSkill->allocatedPhysical,amount);return true;
    }
    return false;
}
DomainResult<bool> System::sampleNativeSkillAccuracy(const ActorContext &actor,const SkillCastSpec &skill,const WeaponDamage &weapon,EntityId victim,std::shared_ptr<NativeSkillContact> &cursor) {
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto source=participants.find(actor.actor),target=participants.find(victim);
    if(!source.aliveIn(actor.area) || !target.aliveIn(actor.area) || !participants.canHarm(actor.actor,victim)) return {DomainStatus::Unavailable,{}};
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return {DomainStatus::InvalidActor,{}};
    if(!cursor) cursor=std::make_shared<NativeSkillContact>();
    cursor->melee=cursor->accuracyOnly=true;cursor->skill=skill.sourceId;cursor->rank=skill.rank;
    SpellImpact impact;impact.source=actor.actor;impact.area=actor.area;impact.targets={victim};impact.nativeSkill=cursor;
    const auto modifiers=source.player?source.player->totals.character.combat:ports_.effects.combatModifiers(source.id(),actor.tick);
    impact.weapon=prepareWeaponSkillProcedures(weapon,modifiers,skill,caster->level(),false);
    impact.missileDamage=std::make_shared<MissileDamageRollState>();impact.missileDamage->formulas=caster->programs()->formulas;
    const auto prepared=prepareNativeSkillContact(impact,source,target,{actor.tick});
    return {prepared,prepared==DomainStatus::Applied?std::optional{cursor->hit}:std::nullopt};
}
DomainResult<bool> System::prepareWeaponBlock(ParticipantView target,TickContext tick,WeaponBlockCursor &cursor) {
    if(cursor.target!=target.id()) cursor={target.id()};
    if(!cursor.checked) {
        const auto *gear=target.player?&target.player->totals.equipment:
            target.monster && target.monster->equipmentStats?&*target.monster->equipmentStats:nullptr;
        if(!gear || gear->animationClass!="ht2" || target.moving()) {cursor.checked=true;return {DomainStatus::Applied,false};}
        const auto stats=target.player?std::optional{target.player->totals.nativeStats}:ports_.effects.unitStatValues(target.id(),tick.tick);
        const auto *rules=target.player?target.player->rules.character->stats.get():target.monster->rule.stats.get();
        if(!stats || !rules) return {DomainStatus::Unavailable,{}};
        const int chance=nativeWeaponBlockChance(*gear,*stats,*rules);
        if(chance>0) {
            const auto expected=target.player?target.player->combatRandom:target.monster->combatRandom;auto random=expected;
            const bool blocked=int(rollRandom(random)%100)<chance;
            const auto consumed=target.player?ports_.transactions.consumeRandom(
                {target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick},expected,random):
                ports_.monsters.consumeRandom(target.id(),expected,random);
            if(!consumed) return {consumed.status,{}};
            cursor.blocked=blocked;
        }
        cursor.checked=true;
    }
    if(cursor.blocked && !cursor.reactionComplete) {
        const auto result=target.player?ports_.effects.block({target.player->player,target.id(),target.player->area,
            ports_.areas.at(target.player->area).generation,0,tick.tick}):ports_.monsters.block(target.id(),tick.tick);
        if(!result) return {result.status,{}};
        cursor.reactionComplete=true;
    }
    return {DomainStatus::Applied,cursor.blocked};
}
DomainStatus System::prepareNativeSkillContact(SpellImpact &impact,ParticipantView source,ParticipantView target,TickContext tick) {
    auto &execution=*impact.nativeSkill;const auto &catalog=*impact.missileDamage->formulas;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source.id());
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    const auto &programs=*caster->programs();
    const auto sourceStats=[&]() {
        if(source.player) {
            const auto hand=impact.weapon?source.player->totals.handStats.find(impact.weapon->weapon.item):source.player->totals.handStats.end();
            return std::optional<StatSnapshot>{hand==source.player->totals.handStats.end()?source.player->totals.nativeStats:hand->second};
        }
        const auto hand=impact.weapon && impact.weapon->weapon.item?std::optional{impact.weapon->weapon.item}:std::nullopt;
        return ports_.effects.unitStatValues(source.id(),tick.tick,hand,execution.monsterProgram?execution.monsterProgram->modeStats:nullptr);
    };
    const auto typeBonus=[&](std::string_view name) {
        if(!target.monster || !programs.monsterTypes) return int32_t{};
        const auto &types=programs.monsterTypes->classes;
        const auto found=types.find(target.monster->rule.nativeClass);if(found==types.end()) return int32_t{};
        const auto stats=sourceStats();if(!stats) return int32_t{};
        return nativeMonsterTypeBonus(*stats,programs.stats->names.at(std::string(name)),found->second);
    };
    const auto consume=[&](uint64_t expected,uint64_t next) {
        if(next==expected) return DomainStatus::Applied;
        if(source.monster) return ports_.monsters.consumeRandom(source.id(),expected,next).status;
        const auto &player=*source.player;
        return ports_.transactions.consumeRandom({player.player,player.actor,player.area,ports_.areas.at(player.area).generation,0,tick.tick},expected,next).status;
    };
    const auto evaluate=[&](uint64_t &random,const auto &operation) {
        const auto stats=source.player?std::optional{source.player->totals.nativeStats}:ports_.effects.unitStatValues(source.id(),tick.tick);
        if(!stats) throw std::logic_error("Missing native direct skill owner stats");
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        auto input=source.player?skills::formulaInput(*source.player,draw):skills::formulaInput(*source.monster,*stats,draw);
        SkillFormulaValues values(catalog,input);return operation(values);
    };
    if(execution.staticField && !execution.rolled) {
        const auto &program=*execution.staticField;
        const auto type=[&] {
            switch(program.element) {
            case MissileDamageElement::Fire:return DamageType::Fire;
            case MissileDamageElement::Lightning:return DamageType::Lightning;
            case MissileDamageElement::Magic:return DamageType::Magic;
            case MissileDamageElement::Cold:case MissileDamageElement::Freeze:return DamageType::Cold;
            case MissileDamageElement::Poison:return DamageType::Poison;
            default:return DamageType::Physical;
            }
        };
        const auto life=target.player?target.player->persistent.player.hp.raw():nativeChecked(target.monster->life);
        const auto maximum=target.player?target.player->totals.character.maxLifeFixed.raw():nativeChecked(target.monster->maximumLife);
        const auto amount=staticFieldDamageFixed(life,maximum,program.percent,program.minimumPercent,program.minimumDamage,
            target.player?rawResistance(target.player->totals.character,type()):ports_.effects.unitRawResistance(target.id(),type(),tick.tick));
        if(!amount) {execution.hit=false;execution.rolled=true;return DomainStatus::Applied;}
        const auto expected=caster->random();auto random=expected;MissileHitDamage damage;
        addNativeSkillElement(damage,program.element,*amount,program.length,random);damage.hitClass|=0xd;
        const auto accepted=consume(expected,random);if(accepted!=DomainStatus::Applied) return accepted;
        impact.areaDamage->rolled->value=damage;impact.areaDamage->resultFlags=0x4001;impact.hitClass=damage.hitClass&0xf;
        execution.rolled=true;return DomainStatus::Applied;
    }
    const bool smite=execution.melee && impact.weapon && impact.weapon->wearSkill.weapon->doFunction==150 && source.player;
    const bool basicKick=execution.melee && impact.weapon && impact.weapon->wearSkill.weapon->startFunction==2;
    const bool monsterSmite=execution.melee && impact.weapon && impact.weapon->wearSkill.weapon->doFunction==150 && source.monster;
    const bool fireHit=execution.melee && impact.weapon && impact.weapon->wearSkill.weapon->startFunction==42 && source.monster;
    const bool vengeance=execution.melee && impact.weapon && impact.weapon->wearSkill.weapon->startFunction==35 && impact.weapon->wearSkill.weapon->doFunction==2;
    const bool startProgram=execution.melee && impact.weapon && !vengeance && !fireHit && !basicKick && (impact.weapon->wearSkill.weapon->allocatesMeleeAtStart() ||
        impact.weapon->wearSkill.weapon->doFunction==7 || impact.weapon->wearSkill.weapon->doFunction==9 || impact.weapon->wearSkill.weapon->doFunction==67 ||
        impact.weapon->wearSkill.weapon->doFunction==70 || impact.weapon->wearSkill.weapon->doFunction==76 || impact.weapon->wearSkill.weapon->doFunction==78);
    const bool repeatedProgram=execution.melee && impact.weapon &&
        (impact.weapon->wearSkill.weapon->doFunction==13 || impact.weapon->wearSkill.weapon->doFunction==109);
    const bool prefilledPhysical=startProgram && (impact.weapon->wearSkill.weapon->startFunction==7 || impact.weapon->wearSkill.weapon->startFunction==29);
    const auto monsterProfile=[&](uint8_t mode)->DomainStatus {
        if(!source.monster || !execution.monsterProgram || execution.monsterProfilePrepared) return DomainStatus::Applied;
        const auto &unit=*source.monster;const auto &rules=unit.rule;
        const auto expected=unit.combatRandom;auto random=expected;
        const auto profile=prepareMonsterModeStats(*rules.modeTables.at(size_t(rules.difficulty)),*rules.stats,
            {mode,rules.level,rules.difficulty,rules.statGameType,unit.admittedPlayerCount,
             unit.alignment()==UnitAlignment::Evil,execution.monsterProgram->ownerPhysical,unit.id,tick.tick},random);
        const auto stats=ports_.effects.unitStatValues(unit.id,tick.tick,impact.weapon->weapon.item?
            std::optional{impact.weapon->weapon.item}:std::nullopt,profile);
        if(!stats) return DomainStatus::Unavailable;
        const auto modifiers=ports_.effects.combatModifiers(unit.id,tick.tick);
        auto sample=sampleNativeWeapon(impact.weapon->weapon,*stats,*programs.stats,modifiers,true);
        if(rules.enchantment) {
            sample.statDamagePercent=nativeAdd(sample.statDamagePercent,rules.enchantment->damagePercent);
            sample.damagePercent=nativeAdd(sample.damagePercent,rules.enchantment->damagePercent);
            sample.attackRatingPercent=nativeAdd(sample.attackRatingPercent,rules.enchantment->attackRatingPercent);
        }
        auto staged=prepareWeaponSkillProcedures(sample,modifiers,impact.weapon->wearSkill,impact.weapon->level,false);
        staged.automatic=impact.weapon->automatic;staged.stunFrames=impact.weapon->stunFrames;
        staged.knockback|=impact.weapon->knockback;staged.hitClass=impact.weapon->hitClass;
        const auto accepted=ports_.monsters.attackProfile(unit.id,profile,expected,random);if(!accepted) return accepted.status;
        execution.monsterProgram->modeStats=profile;impact.weapon=std::move(staged);
        execution.monsterProfilePrepared=true;return DomainStatus::Applied;
    };
    if(execution.melee && (impact.weapon->wearSkill.weapon->startFunction==32 || impact.weapon->wearSkill.weapon->doFunction==70) && !execution.modeRatePrepared) {
        const auto expected=caster->random();auto random=expected;
        const auto rate=evaluate(random,[&](SkillFormulaValues &values) {
            const auto value=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,"calc3");
            if(!value) throw std::logic_error("Missing native St32 mode attack-rate formula");return *value;
        });
        const auto posted=ports_.effects.postModeRate(source.id(),execution.skill,execution.rank,rate,execution.modeRevision,tick.tick,{expected,random});
        if(!posted) return posted.status;
        execution.modeRatePrepared=true;
    }
    if(smite) {
        const auto prepared=prepareNativeSmite(impact,*source.player,tick);if(prepared!=DomainStatus::Applied) return prepared;
    }
    if(monsterSmite && !execution.programPrefilled) {
        const auto profile=monsterProfile(5);if(profile!=DomainStatus::Applied) return profile;
        const auto expected=caster->random();auto random=expected;
        const auto stun=evaluate(random,[&](SkillFormulaValues &values) {
            const auto value=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,"calc2");
            if(!value) throw std::logic_error("Missing native monster Smite stun formula");return *value;
        });
        const auto consumed=consume(expected,random);if(consumed!=DomainStatus::Applied) return consumed;
        impact.weapon->stunFrames=stun;impact.weapon->hitClass=0x65;execution.sourceDamage=128;execution.programPrefilled=true;
    }
    if(basicKick && !execution.rolled) {
        const auto &row=catalog.skills.at(execution.skill);MissileHitDamage damage;
        damage.channels[0]=nativeShiftLeft(row.number("mindam"),unsigned(row.number("hitshift")));
        damage.hitClass=1;damage.knockback=true;
        impact.areaDamage->hitFlags=2;impact.areaDamage->resultFlags=9;impact.areaDamage->rolled->value=damage;
        impact.weapon->nativeFixedPhysical=true;impact.weapon->channels[0]=damage.channels[0];
        impact.weapon->lifeLeech=impact.weapon->manaLeech=0;
        impact.weapon->physicalFlat=impact.weapon->conversionPercent=0;
        execution.hit=execution.accuracyHit=execution.accuracyPrepared=execution.defensePrepared=true;
        execution.rolled=execution.targetArmorPrepared=execution.fillEventComplete=execution.elementsComplete=true;
    }
    if(execution.melee) {
        if(!impact.weapon) return DomainStatus::Unavailable;
        if(!execution.accuracyPrepared) {
            const auto expected=caster->random();auto random=expected;
            auto attack=*impact.weapon;
            if(smite || monsterSmite || fireHit || startProgram || repeatedProgram || vengeance) {
                const auto toHit=smite || monsterSmite || (execution.monsterProgram && execution.monsterProgram->alwaysHit) ||
                    (startProgram && (attack.wearSkill.weapon->startFunction==6 || attack.wearSkill.weapon->startFunction==10))?0:evaluate(random,[&](SkillFormulaValues &values) {
                    const auto value=values.parameter(FormulaDomain::Skill,execution.skill,execution.rank,"toht");
                    if(!value) throw std::logic_error("Missing native melee to-hit formula");return *value;
                });
                attack.weapon.attackRatingPercent=nativeAdd(nativeSubtract(attack.weapon.attackRatingPercent,attack.wearSkill.weapon->attackRating),toHit);
            }
            attack.weapon.attackRatingPercent=nativeAdd(attack.weapon.attackRatingPercent,typeBonus("attack_vs_montype"));
            if(execution.progressive) {
                const auto stats=sourceStats();if(!stats) return DomainStatus::Unavailable;
                attack.weapon.attackRatingPercent=nativeAdd(attack.weapon.attackRatingPercent,stats->get({programs.stats->names.at("progressive_tohit"),0}));
            }
            const bool alive=source.player?source.player->persistent.player.hp>0:source.monster->alive();
            const bool hit=alive && nativeMeleeReach(impact) && weaponContact(attack,target.id(),tick.tick,random);
            const auto result=consume(expected,random);if(result!=DomainStatus::Applied) return result;
            execution.accuracyHit=hit;execution.hit=hit || smite;execution.accuracyPrepared=true;
            // Do150 ORs SUCCESSFULHIT after GetResultFlags. An accuracy miss
            // never entered block/dodge, so forcing success must not roll it.
            if(smite && !hit) execution.defensePrepared=true;
            if(execution.monsterProgram && execution.monsterProgram->alwaysHit) {
                execution.defensePrepared=true;impact.areaDamage->resultFlags=9;
            }
        }
        if(execution.hit && !execution.defensePrepared) {
            const auto expected=target.player?target.player->combatRandom:target.monster->combatRandom;auto random=expected;
            const auto stats=target.player?std::optional{target.player->totals.nativeStats}:ports_.effects.unitStatValues(target.id(),tick.tick);
            if(!stats) return DomainStatus::Unavailable;
            const auto &rules=target.player?*target.player->rules.skills->stats:*target.monster->rule.stats;
            int block=target.player && target.player->totals.equipment.shield?target.player->totals.equipment.blockChance:
                target.monster && (target.monster->shield || target.monster->rule.blockWithoutShield || target.monster->rule.nativeBlockException)?
                    std::min(75,stats->get({rules.names.at("toblock"),0})):0;
            if(target.player && target.player->moving && target.player->runningNow) block/=3;
            const auto mods=target.player?target.player->totals.character.combat:ports_.effects.combatModifiers(target.id(),tick.tick);
            const auto *equipment=target.player?&target.player->totals.equipment:
                target.monster->equipmentStats?&*target.monster->equipmentStats:nullptr;
            const auto defense=rollNativeBlockOrDodge(block,true,target.moving(),
                equipment?nativeWeaponBlockChance(*equipment,*stats,rules):0,
                equipment && equipment->animationClass=="ht2",mods,false,random);
            const auto result=target.monster?ports_.monsters.consumeRandom(target.id(),expected,random).status:
                ports_.transactions.consumeRandom({target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick},expected,random).status;
            if(result!=DomainStatus::Applied) return result;
            execution.defense=defense;execution.hit=(!defense.block || (smite && !defense.weaponBlock)) && defense.avoidance==WeaponAvoidance::None;execution.defensePrepared=true;
        }
        if(smite) {
            // Smite forces only SUCCESSFULHIT, not GetResultFlags' GETHIT.
            // A failed accuracy roll or a blocked hit must retain that distinction.
            const auto &defense=execution.defense;
            uint16_t result=1;
            if(execution.accuracyHit && !defense.block && defense.avoidance==WeaponAvoidance::None) result|=4;
            if(defense.block) result|=defense.weaponBlock?0x8000:0x10;
            if(defense.avoidance==WeaponAvoidance::Dodge) result|=0x80;
            else if(defense.avoidance==WeaponAvoidance::Avoid) result|=0x100;
            else if(defense.avoidance==WeaponAvoidance::Evade) result|=0x200;
            impact.areaDamage->resultFlags=result|uint16_t(catalog.skills.at(execution.skill).number("resultflags"));
        }
        if(execution.accuracyOnly) return DomainStatus::Applied;
        // SrvDo001 calls FillDamageValues even on a miss/block. Other migrated
        // skill programs enter FillDamageValues only after a successful hit.
        if(!execution.hit && !impact.weapon->wearSkill.weapon->commonAttack && impact.weapon->wearSkill.weapon->doFunction!=67)
            return finishNativeMeleeMiss(impact,target,tick);
        if(execution.hit) impact.unblockable=true; // GetResultFlags already consumed block/dodge.
        if((fireHit || monsterSmite) && execution.hit) {
            if(fireHit) {const auto profile=monsterProfile(8);if(profile!=DomainStatus::Applied) return profile;execution.sourceDamage=128;}
            const auto &row=catalog.skills.at(execution.skill);
            impact.areaDamage->resultFlags|=uint16_t(row.number("resultflags"));
            impact.areaDamage->hitFlags|=uint32_t(row.number("hitflags"));
            impact.weapon->knockback|=(row.number("resultflags")&8)!=0;
            if(row.number("hitclass")) impact.weapon->hitClass=row.number("hitclass");
        }
    }
    if((startProgram || repeatedProgram) && !execution.programPrefilled) {
        // The skill callback evaluates ED/conversion and rolls its own element
        // before AllocCombat enters Fill (target AC, weapon and equipment).
        const auto expected=caster->random();auto random=expected;
        auto skill=impact.weapon->wearSkill;
        auto stagedWeapon=*impact.weapon;
        auto damage=execution.initialDamage?*execution.initialDamage:MissileHitDamage{};
        std::optional<unsigned> impaleWear;
        const auto &row=catalog.skills.at(skill.sourceId);
        evaluate(random,[&](SkillFormulaValues &values) {
            const auto percent=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,repeatedProgram?"calc2":"calc1");
            if(!percent) throw std::logic_error("Missing native melee damage percentage");
            skill.weapon->damagePercent=*percent;
            if(prefilledPhysical) {
                const auto modifiers=source.player?source.player->totals.character.combat:ports_.effects.combatModifiers(source.id(),tick.tick);
                auto physical=rollNativeWeaponPhysical(impact.weapon->weapon,modifiers,skill,impact.weapon->level,false,false,random,
                    {row.number("srcdam"),0,false});
                physical.nativeFixedPhysical=true;
                damage.channels[0]=nativeChecked(physical.channels[0]);
                stagedWeapon=std::move(physical);
            }
            if(row.elementType) {
                const auto conversion=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,"calc4");
                if(!conversion) throw std::logic_error("Missing native melee conversion percentage");
                skill.weapon->magicConversionPercent=*conversion;
            }
            if(skill.weapon->startFunction==32) skill.weapon->physicalFlat=0;
            if(skill.weapon->startFunction==10) {
                const auto low=values.parameter(FormulaDomain::Skill,execution.skill,execution.rank,"enms");
                const auto high=values.parameter(FormulaDomain::Skill,execution.skill,execution.rank,"exms");
                if(!low || !high) throw std::logic_error("Missing native Lightning Strike initial damage");
                damage.channels[3]=nativeAdd(*low,int32_t(limitedRandom(random,uint32_t(nativeSubtract(*high,*low)))));
            } else if((startProgram && skill.weapon->startFunction!=16) || row.elementType)
                rollNativeSkillElement(damage,values,execution.skill,execution.rank,random);
            if(skill.weapon->startFunction==7 && source.player && impact.weapon->weapon.item) {
                const auto &player=*source.player;
                const auto found=player.persistent.inventory.items.find(impact.weapon->weapon.item);
                if(found!=player.persistent.inventory.items.end()) {
                    const auto &properties=player.rules.equipment->at(found->first,player.persistent.player.level);
                    if(properties.maximumDurability && !std::any_of(properties.stats.begin(),properties.stats.end(),
                        [](const auto &stat){return stat.effect=="item_indesctructible" && stat.value;})) {
                        const auto chance=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,"calc2");
                        const auto amount=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,"calc3");
                        if(!chance || !amount) throw std::logic_error("Missing native Impale durability formulas");
                        if(int32_t(rollRandom(random)%100)<*chance && *amount>0) impaleWear=unsigned(*amount);
                    }
                }
            }
            return 0;
        });
        if(impaleWear) {
            const auto actor=caster->context(ports_.areas,tick.tick);
            auto wear=ports_.inventory.weaponCost(actor,impact.weapon->weapon,skill,false,false,*impaleWear);
            if(!wear) return wear.status;
            wear.value->random=transactions::UnitRandomEdit{expected,random};
            const auto committed=ports_.transactions.commit(std::move(*wear.value));if(!committed) return committed.status;
        } else {const auto committed=consume(expected,random);if(committed!=DomainStatus::Applied) return committed;}
        if(prefilledPhysical) impact.areaDamage->hitFlags|=1;
        if((startProgram && skill.weapon->startFunction!=6 && skill.weapon->startFunction!=7 && skill.weapon->startFunction!=10 && skill.weapon->startFunction!=16) ||
           (repeatedProgram && skill.weapon->doFunction==109)) {
            impact.areaDamage->resultFlags|=uint16_t(row.number("resultflags"));
            impact.areaDamage->hitFlags|=uint32_t(row.number("hitflags"));
            if(row.number("hitclass")) damage.hitClass=uint8_t(row.number("hitclass"));
        }
        damage.knockback|=(impact.areaDamage->resultFlags&8)!=0;
        if(skill.weapon->doFunction==67 || skill.weapon->doFunction==78) {
            damage.knockback=true;impact.areaDamage->resultFlags|=8;
            if(skill.weapon->doFunction==67) damage.hitClass=0x70;
        }
        const auto srcDamage=row.number("srcdam");execution.sourceDamage=srcDamage?srcDamage:128;
        stagedWeapon.wearSkill=std::move(skill);
        impact.weapon=std::move(stagedWeapon);
        if(impact.weapon->wearSkill.weapon->startFunction==7) {
            impact.weapon->wearChance=impact.weapon->weapon.item?4:0;impact.weapon->wearAmount=1;
        }
        execution.initialDamage=std::make_shared<MissileHitDamage>(std::move(damage));execution.programPrefilled=true;
    }
    if(execution.melee && !execution.hit && !impact.weapon->wearSkill.weapon->commonAttack)
        return finishNativeMeleeMiss(impact,target,tick);
    if(execution.melee && source.monster && impact.weapon->wearSkill.weapon->doFunction==67) {
        // SkillPal Do67 selects the original class-specific damage mode.
        // These native identities are algorithm cases, not MPQ tuning values.
        constexpr int clawViper=73,duriel=211,reanimatedHorde=436;
        const auto nativeClass=source.monster->rule.nativeClass;
        const uint8_t mode=nativeClass==clawViper?8:(nativeClass==duriel || nativeClass==reanimatedHorde)?5:4;
        const auto profile=monsterProfile(mode);if(profile!=DomainStatus::Applied) return profile;
    }
    if(execution.hit && execution.progressive && !execution.overlayPrepared) {
        std::vector<DomainFact> facts;
        for(const auto &charge:ports_.effects.progressiveCharges(source.id())) if(charge.program->progressiveDamage==1 && charge.program->targetOverlay>0)
            facts.emplace_back(OverlayFact{target.id(),uint8_t(target.player?0:1),impact.area,charge.program->targetOverlay});
        if(!facts.empty()) {const auto result=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},impact.area},std::move(facts)});if(!result) return result.status;}
        execution.overlayPrepared=true;
    }
    const auto prepareTargetArmor=[&]()->DomainStatus {
        if(!execution.melee || execution.targetArmorPrepared) return DomainStatus::Applied;
        // FillDamageValues writes target AC after GetResultFlags, even when
        // SrvDo001 missed. It must affect later attacks, not this hit roll.
        if(target.monster && (source.player || source.monster->alignment()==UnitAlignment::Good)) {
            const auto stats=sourceStats();if(!stats) return DomainStatus::Unavailable;
            const auto adjustment=stats->get({programs.stats->names.at("item_damagetargetac"),0});
            if(adjustment) {
                const auto &defender=*target.monster;if(defender.revision==UINT64_MAX) return DomainStatus::Capacity;
                attributes::MonsterResourceTrace resources(defender,tick.tick);
                const auto sources=ports_.effects.unitStatSources(defender.id,tick.tick);resources.sampleSources(sources);
                const auto current=resources.values(sources).get({defender.rule.stats->names.at("armorclass"),0});
                resources.writeBaseStat("armorclass",std::max(0,nativeAdd(current,adjustment)),sources);
                auto change=resources.take();
                if(!ports_.monsters.canCommitLife(change)) return DomainStatus::Stale;
                if(!ports_.monsters.commitLife(std::move(change))) throw std::logic_error("Melee target armor lost its source reservation");
            }
        }
        execution.targetArmorPrepared=true;
        return DomainStatus::Applied;
    };
    if(!vengeance) {const auto prepared=prepareTargetArmor();if(prepared!=DomainStatus::Applied) return prepared;}
    if(!execution.rolled) {
        const auto expected=source.player?source.player->combatRandom:source.monster->combatRandom;auto random=expected;
        auto enchantment=source.monster?source.monster->enchantmentDamage:MonsterEnchantmentDamageState{};
        auto weapon=impact.weapon;
        const auto damage=evaluate(random,[&](SkillFormulaValues &values) {
            if(!execution.melee) {
                auto damage=execution.directFunction==100?MissileHitDamage{}:rollNativeSkillDamage(values,execution.skill,execution.rank,random);
                if(execution.directFunction==100) {
                    const auto length=values.parameter(FormulaDomain::Skill,execution.skill,execution.rank,"edln");
                    if(!length) throw std::logic_error("Missing native DiabCold freeze length");
                    damage.freezeLength=*length;
                    rollNativeSkillElement(damage,values,execution.skill,execution.rank,random);
                    if(const auto hitClass=catalog.skills.at(execution.skill).number("hitclass")) damage.hitClass=uint8_t(hitClass);
                }
                if(execution.directFunction==107) {
                    const auto range=values.physicalRange(execution.skill,execution.rank,true);
                    const auto length=values.parameter(FormulaDomain::Skill,execution.skill,execution.rank,"edln");
                    const auto heal=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,"calc3");
                    if(!range || !length || !heal) throw std::logic_error("Missing native Mosquito damage formula");
                    const int low=range->first>>8,high=range->second>>8;
                    const auto drain=[&] {return nativeAdd(low,int(limitedRandom(random,uint32_t(nativeSubtract(high,low)))));};
                    damage.channels[5]=nativeMultiply(2,drain());damage.poisonLength=*length;
                    damage.manaLeech=nativeShiftLeft(drain(),8);damage.staminaLeech=nativeShiftLeft(drain(),8);
                    execution.directHealing=nativePercentage(damage.channels[0],*heal,100);
                }
                if(execution.directFunction==21) {
                    const auto &row=catalog.skills.at(execution.skill);
                    impact.areaDamage->resultFlags=uint16_t(row.number("resultflags"));
                    impact.areaDamage->hitFlags=uint32_t(row.number("hitflags"));
                    if(const auto hitClass=row.number("hitclass")) damage.hitClass=uint8_t(hitClass);
                    impact.hitClass=damage.hitClass&0xf;
                    if(int(rollRandom(random)%100)<row.number("param2")) impact.areaDamage->resultFlags|=9;
                }
                return damage;
            }
            auto skill=weapon->wearSkill;
            const auto &row=catalog.skills.at(skill.sourceId);
            if(!execution.kick) skill.weapon->damagePercent=nativeAdd(skill.weapon->damagePercent,typeBonus("damage_vs_montype"));
            if(execution.hit && execution.progressive) skill.weapon->damagePercent=nativeAdd(skill.weapon->damagePercent,ports_.effects.progressivePhysicalPercent(source.id()));
            const auto sourceDamage=execution.sourceDamage.value_or(catalog.skills.at(skill.sourceId).number("srcdam"));
            const auto base=execution.initialDamage?*execution.initialDamage:MissileHitDamage{};
            const auto stats=sourceStats();if(!stats) throw std::logic_error("Missing native melee source stat view");
            const auto modifiers=source.player?source.player->totals.character.combat:ports_.effects.combatModifiers(source.id(),tick.tick);
            auto equipment=base;
            if(skill.druid && (skill.druid->program==DruidProgram::Rabies || skill.druid->program==DruidProgram::BearSmite)) {
                rollNativeSkillElement(equipment,values,execution.skill,execution.rank,random);
                if(skill.druid->program==DruidProgram::Rabies) equipment.poisonLength=std::max(equipment.poisonLength,10);
            }
            if(execution.kick) {
                const auto unmerged=ports_.effects.unitStatValues(source.id(),tick.tick,EntityId{});
                if(!unmerged) throw std::logic_error("Missing native kick unmerged weapon view");
                const auto stat=[&](std::string_view name){return unmerged->get({programs.stats->names.at(std::string(name)),0});};
                const auto *gear=source.player?&source.player->totals.equipment:source.monster->equipmentStats?&*source.monster->equipmentStats:nullptr;
                const auto boots=gear?gear->kickBoots:EquipmentStats::KickBoots{};
                AssassinKickInput input{stat("strength"),stat("dexterity"),skill.weapon->damagePercent,stat("item_kickdamage"),
                    boots.minimum,boots.maximum,boots.strengthBonus,boots.dexterityBonus,stat("damagepercent"),stat("item_maxdamage_percent"),boots.equipped};
                const auto [low,high]=assassinKickRange(input);
                equipment.channels[0]=nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeSubtract(high,low)))));
                rollNativeSkillElement(equipment,values,execution.skill,execution.rank,random);
                fillNativeWeaponElements(equipment,*unmerged,*programs.stats,128,random,source.monster && !source.monster->hireling);
                weapon->nativeFixedPhysical=true;weapon->physicalFlat=0;weapon->critical=false;
                equipment.knockback=execution.kickKnockback;
                impact.areaDamage->hitFlags=0x23;
                if(execution.kickKnockback) impact.areaDamage->resultFlags|=0xc;
            } else if(vengeance) {
                // St35 pre-fills physical damage (HitFlags 1). Fill must not
                // reroll it, apply target ED or consume critical/deadly RNG.
                skill.weapon->damagePercent=skill.weapon->magicConversionPercent=0;
                *weapon=rollNativeWeaponPhysical(weapon->weapon,modifiers,skill,weapon->level,false,false,random,{sourceDamage,0,false});
                weapon->nativeFixedPhysical=true;
                equipment.channels[0]=nativeChecked(weapon->channels[0]);
                const auto stat=[&](std::string_view name){return stats->get({programs.stats->names.at(std::string(name)),0});};
                auto low=stat(weapon->weapon.twoHanded?"secondary_mindamage":"mindamage");
                auto high=stat(weapon->weapon.twoHanded?"secondary_maxdamage":"maxdamage");
                if(!weapon->weapon.item) {low=std::max(low,1);high=std::max(high,2);}
                low=nativeShiftLeft(low,8);high=nativeShiftLeft(high,8);
                if(low<1) low=256;
                if(high<=low) high=nativeAdd(low,256);
                const auto elementalBase=nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeSubtract(high,low)))));
                constexpr std::array<size_t,3> channels{2,4,3};
                constexpr std::array<std::string_view,3> formulas{"calc1","calc2","calc3"},masteries{"passive_fire_mastery","passive_cold_mastery","passive_ltng_mastery"};
                for(size_t index=0;index<channels.size();++index) {
                    const auto formula=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,formulas[index]);
                    if(!formula) throw std::logic_error("Missing native Vengeance element percentage");
                    const auto percent=nativeAdd(*formula,nativePercentage(*formula,stat(masteries[index]),100));
                    equipment.channels[channels[index]]=nativePercentage(elementalBase,percent,100);
                }
                const auto length=values.parameter(FormulaDomain::Skill,execution.skill,execution.rank,"edln");
                if(!length) throw std::logic_error("Missing native Vengeance cold length");
                equipment.coldLength=*length;
                if(execution.vengeanceCycle>=0 && execution.vengeanceCycle<=2) equipment.hitClass=uint8_t(32+16*execution.vengeanceCycle);
                impact.areaDamage->resultFlags|=uint16_t(row.number("resultflags"));
                impact.areaDamage->hitFlags|=uint32_t(row.number("hitflags"))|1;
            } else {
                if(!prefilledPhysical) {
                    *weapon=rollNativeWeaponPhysical(weapon->weapon,modifiers,skill,weapon->level,
                        target.monster && target.monster->rule.demon,target.monster && target.monster->rule.undead,random,{sourceDamage,base.channels[0]});
                    equipment.channels[0]=nativeChecked(weapon->channels[0]);
                }
                fillNativeWeaponElements(equipment,*stats,*programs.stats,sourceDamage,random,source.monster && !source.monster->hireling);
            }
            if(execution.monsterProgram && source.monster->rule.enchantment) {
                MonsterHit extra;
                addMonsterEnchantmentDamage(extra,*source.monster->rule.enchantment,sourceDamage,random,enchantment);
                for(size_t channel=0;channel<equipment.channels.size();++channel)
                    equipment.channels[channel]=nativeAdd(equipment.channels[channel],nativeChecked(extra.channels[channel]));
                equipment.coldLength=nativeAdd(equipment.coldLength,nativeChecked(extra.coldFrames));
                equipment.poisonLength=nativeAdd(equipment.poisonLength,nativeChecked(extra.poisonFrames));
                equipment.manaLeech=nativeAdd(equipment.manaLeech,nativeChecked(extra.mana));
            }
            if(source.monster && source.monster->rule.criticalChance>0) {
                execution.monsterCritical=int(rollRandom(random)%100)<source.monster->rule.criticalChance;
                if(execution.monsterCritical) for(auto &channel:equipment.channels) channel=nativeMultiply(channel,2);
            }
            for(size_t channel=0;channel<equipment.channels.size();++channel) weapon->channels[channel]=equipment.channels[channel];
            equipment.knockback|=weapon->knockback;
            equipment.stunLength=std::max(equipment.stunLength,weapon->stunFrames);
            weapon->coldFrames=equipment.coldLength;weapon->poisonFrames=equipment.poisonLength;weapon->stunFrames=equipment.stunLength;
            weapon->lifeLeech=equipment.lifeLeech;weapon->manaLeech=equipment.manaLeech;
            weapon->staminaLeech=equipment.staminaLeech;
            if(equipment.hitClass) weapon->hitClass=equipment.hitClass;
            MissileHitDamage damage=equipment;
            damage.lifeLeech=weapon->lifeLeech;damage.manaLeech=weapon->manaLeech;damage.coldLength=weapon->coldFrames;
            damage.staminaLeech=weapon->staminaLeech;
            damage.freezeLength=weapon->freezeFrames;damage.poisonLength=weapon->poisonFrames;damage.softHit=weapon->softHit;
            damage.stunLength=weapon->stunFrames;damage.critical=weapon->critical;damage.hitClass=uint8_t(weapon->hitClass>=0?weapon->hitClass:weapon->weapon.hitClass);
            damage.knockback=equipment.knockback;
            if(execution.kick && skill.assassin && skill.assassin->program==AssassinProgram::DragonTalon) damage.hitClass=1;
            damage.bypassUndead=equipment.bypassUndead;damage.bypassDemon=equipment.bypassDemon;damage.bypassBeasts=equipment.bypassBeasts;return damage;
        });
        const auto committed=consume(expected,random);if(committed!=DomainStatus::Applied) return committed;
        if(execution.monsterProgram && source.monster->rule.enchantment)
            ports_.monsters.commitEnchantmentDamage(source.id(),std::move(enchantment));
        impact.areaDamage->rolled->value=damage;impact.weapon=std::move(weapon);execution.rolled=true;
    }
    if(execution.directFunction==107 && !execution.directHealingComplete) {
        if(!source.monster) return DomainStatus::InvalidActor;
        const auto seed=source.monster->combatRandom;
        const auto healed=ports_.monsters.healSkill(source.id(),source.id(),std::max(0,execution.directHealing),tick.tick,{seed,seed});
        if(!healed) return healed.status;
        execution.directHealingComplete=true;
    }
    if(vengeance && !execution.vengeanceCyclePosted) {
        const auto &skill=impact.weapon->wearSkill;
        ports_.actions.skillParameter(source.id(),{skill.sourceId,skill.charge?skill.charge->item.id:EntityId{}},0,(execution.vengeanceCycle+1)%3);
        execution.vengeanceCyclePosted=true;
    }
    if(vengeance) {
        // St35 pre-fills and advances the skill parameter before AllocCombat.
        // Only its subsequent Fill writes target AC and samples equipment.
        const auto prepared=prepareTargetArmor();if(prepared!=DomainStatus::Applied) return prepared;
        if(!execution.vengeanceEquipmentFilled) {
            const auto stats=sourceStats();if(!stats) return DomainStatus::Unavailable;
            const auto expected=caster->random();auto random=expected;
            auto damage=*impact.areaDamage->rolled->value;auto weapon=*impact.weapon;
            fillNativeWeaponElements(damage,*stats,*programs.stats,128,random,source.monster && !source.monster->hireling);
            for(size_t channel=0;channel<damage.channels.size();++channel) weapon.channels[channel]=damage.channels[channel];
            weapon.coldFrames=damage.coldLength;weapon.poisonFrames=damage.poisonLength;weapon.stunFrames=damage.stunLength;
            weapon.lifeLeech=damage.lifeLeech;weapon.manaLeech=damage.manaLeech;weapon.staminaLeech=damage.staminaLeech;
            const auto committed=consume(expected,random);if(committed!=DomainStatus::Applied) return committed;
            impact.areaDamage->rolled->value=std::move(damage);impact.weapon=std::move(weapon);execution.vengeanceEquipmentFilled=true;
        }
    }
    if(execution.melee && !execution.fillEventComplete) {
        if((source.player || (source.monster && source.monster->hireling)) && !(impact.areaDamage->resultFlags&0x20)) {
            if(!execution.attempt) execution.attempt=std::make_shared<effects::NativeContactBatch>(ports_.effects.prepareAttackEvents(source.id(),target.id(),tick.tick));
            const auto result=ports_.effects.advanceDamageEvents(*execution.attempt,tick.tick);if(!result) return result.status;
            execution.attempt.reset();
        }
        execution.fillEventComplete=true;
    }
    if(execution.monsterCritical && !execution.monsterCriticalPosted) {
        auto &damage=*impact.areaDamage->rolled->value;
        if(!(damage.hitClass&0xf0)) {
            damage.hitClass|=0x10;if(impact.weapon) impact.weapon->hitClass=damage.hitClass;
        } else {
            const auto posted=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},impact.area},
                {OverlayFact{target.id(),uint8_t(target.player?0:1),impact.area,54}}});
            if(!posted) return posted.status;
        }
        execution.monsterCriticalPosted=true;
    }
    if(execution.melee && !execution.elementsComplete) {
        const auto expected=caster->random();auto random=expected;auto weapon=*impact.weapon;
        if(execution.hit && execution.progressive) ports_.effects.progressiveDamage(source.id(),weapon,random);
        auto damage=*impact.areaDamage->rolled->value;
        const auto channels=targetWeaponChannels(weapon,target.monster && target.monster->rule.demon,target.monster && target.monster->rule.undead);
        for(size_t channel=0;channel<channels.size();++channel) damage.channels[channel]=nativeChecked(channels[channel]);
        damage.lifeLeech=weapon.lifeLeech;damage.manaLeech=weapon.manaLeech;damage.staminaLeech=weapon.staminaLeech;
        damage.coldLength=weapon.coldFrames;damage.poisonLength=weapon.poisonFrames;damage.freezeLength=weapon.freezeFrames;
        if(weapon.wearSkill.druid) {
            const auto program=weapon.wearSkill.druid->program;
            if(program==DruidProgram::FireClaws || program==DruidProgram::Hunger) {
                evaluate(random,[&](SkillFormulaValues &values) {
                    rollNativeSkillElement(damage,values,execution.skill,execution.rank,random);
                    if(program==DruidProgram::Hunger) {
                        const auto calculation=[&](std::string_view field) {const auto value=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,field);
                            if(!value) throw std::logic_error("Missing native Hunger formula");return *value;};
                        damage.channels[0]=nativeAdd(damage.channels[0],nativePercentage(damage.channels[0],calculation("calc1"),100));
                        damage.lifeLeech=nativeAdd(damage.lifeLeech,calculation("calc2"));
                        damage.manaLeech=nativeAdd(damage.manaLeech,calculation("calc3"));
                    }
                    return 0;
                });
                for(size_t channel=0;channel<damage.channels.size();++channel) weapon.channels[channel]=damage.channels[channel];
                weapon.lifeLeech=damage.lifeLeech;weapon.manaLeech=damage.manaLeech;
                weapon.coldFrames=damage.coldLength;weapon.poisonFrames=damage.poisonLength;
            }
        }
        if(weapon.hitClass>=0) damage.hitClass=uint8_t(weapon.hitClass);
        const auto result=consume(expected,random);if(result!=DomainStatus::Applied) return result;
        impact.weapon=std::move(weapon);impact.areaDamage->rolled->value=std::move(damage);execution.elementsComplete=true;
    }
    if(execution.melee && !execution.hit) return finishNativeMeleeMiss(impact,target,tick);
    return DomainStatus::Applied;
}
DomainStatus System::finishNativeMeleeMiss(SpellImpact &impact,ParticipantView target,TickContext tick) {
    auto &execution=*impact.nativeSkill;
    if(impact.nativeDrainHeld || execution.skipMissDrain || !nativeMeleeDrainReach(impact)) return DomainStatus::Applied;
    // Drain callbacks precede ExecuteMissileDamage's final block/dodge mode.
    const auto drained=finishNativeMeleeDrain(impact,0,tick);if(drained!=DomainStatus::Applied) return drained;
    if(impact.weapon->wearSkill.weapon->doFunction==67) {
        const auto finished=finishNativeSkillFlags(impact,tick);if(finished!=DomainStatus::Applied) return finished;
    }
    if(!execution.reactionPrepared) {
        DomainStatus status=DomainStatus::Applied;
        if(execution.defense.block) status=target.monster?ports_.monsters.block(target.id(),tick.tick).status:
            ports_.effects.block({target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick}).status;
        else if(target.player && execution.defense.avoidance!=WeaponAvoidance::None)
            status=ports_.effects.avoidance({target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick},execution.defense.avoidance,impact.source).status;
        if(isDeferred(status)) return status;
        execution.reactionPrepared=true;
    }
    return DomainStatus::Applied;
}
DomainStatus System::postNativeSkillCharge(SpellImpact &impact,TickContext tick) {
    if(!impact.nativeSkill || !impact.nativeSkill->melee || impact.nativeSkill->chargePosted) return DomainStatus::Applied;
    auto &execution=*impact.nativeSkill;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,impact.source);if(!caster) return DomainStatus::Stale;
    const auto &skill=impact.weapon->wearSkill;
    const bool monsterFrenzy=skill.weapon && skill.weapon->doFunction==109;
    // Do109 applies its stack after the complete Drain callback, including
    // the defender's damage reaction. Progressive charges precede Drain.
    if(monsterFrenzy && !execution.actionComplete) return DomainStatus::Applied;
    if(execution.hit) {
        if(skill.assassin && skill.assassin->chargeStat>=0) {
            const auto result=ports_.effects.progressiveCharge(caster->context(ports_.areas,tick.tick),skill);
            if(!result) return result.status;
        }
        if(monsterFrenzy) {
            const auto result=ports_.effects.frenzy(caster->context(ports_.areas,tick.tick),skill);
            if(!result) return result.status;
        }
    }
    execution.chargePosted=true;
    return DomainStatus::Applied;
}
bool System::nativeMeleeReach(const SpellImpact &impact,int range) const {
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto source=participants.find(impact.source),target=participants.find(impact.targets.at(impact.next));
    if(!source.id() || !target.id()) return false;
    const auto sourceArea=source.player?source.player->area:source.monster->area,targetArea=target.player?target.player->area:target.monster->area;
    const auto &area=ports_.areas.at(sourceArea);
    const auto point=target.position()+ports_.areas.at(targetArea).definition.origin-area.definition.origin;
    return unitMeleeReach(area.definition.collision,source.position(),source.size(),point,target.size(),
        source.player?source.player->totals.equipment.weapons[0].rangeAdder:source.monster->rule.meleeRange,range<0?(source.player?1:3):range);
}
bool System::nativeMeleeDrainReach(SpellImpact &impact) {
    if(!impact.nativeSkill || !impact.nativeSkill->melee || impact.nativeDrainHeld || impact.nativeSkill->drainRangeChecked) return true;
    if(!nativeMeleeReach(impact)) return false;
    impact.nativeSkill->drainRangeChecked=true;return true;
}
DomainStatus System::finishNativeMeleeDrain(SpellImpact &impact,int64_t physical,TickContext tick) {
    if(!impact.nativeSkill || !impact.nativeSkill->melee) return DomainStatus::Applied;
    auto &execution=*impact.nativeSkill;
    if(!execution.attackItemsComplete) {
        if(const auto *player=ports_.players.findActor(impact.source)) {
            if(!execution.attackItems) {
                const auto target=Participants{ports_.players,ports_.monsters,ports_.social}.find(impact.targets.at(impact.next));
                const ActorContext actor{player->player,player->actor,player->area,ports_.areas.at(player->area).generation,0,tick.tick};
                auto events=ports_.itemTriggers.prepare(actor,{ItemSkillEvent::Attack},target.id(),target.position());
                if(!events) return events.status;
                execution.attackItems=std::make_shared<reactions::ItemEventPlan>(std::move(*events.value));
            }
            if(ports_.itemTriggers.advance(*execution.attackItems,tick.tick)==StepStatus::Blocked) return DomainStatus::Capacity;
        }
        execution.attackItemsComplete=true;
    }
    if(!execution.drainEventsComplete) {
        if(!execution.drainEvents) execution.drainEvents=std::make_shared<effects::NativeContactBatch>(ports_.effects.prepareMeleeDrainEvents(impact.source,impact.targets.at(impact.next),tick.tick));
        const auto result=ports_.effects.advanceDamageEvents(*execution.drainEvents,tick.tick);if(!result) return result.status;
        execution.drainEventsComplete=true;
    }
    if(!execution.thornsComplete) {
        if(!execution.thornsEvents) execution.thornsEvents=std::make_shared<effects::NativeContactBatch>(ports_.effects.prepareThornsEvents(impact.source,impact.targets.at(impact.next),physical,tick.tick));
        const auto result=ports_.effects.advanceDamageEvents(*execution.thornsEvents,tick.tick);if(!result) return result.status;
        execution.thornsComplete=true;
    }
    return DomainStatus::Applied;
}
DomainStatus System::finishNativeSkillFlags(SpellImpact &impact,TickContext tick) {
    if(!impact.nativeSkill || impact.nativeSkill->flagsComplete) return DomainStatus::Applied;
    const auto &execution=*impact.nativeSkill;
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto source=participants.find(impact.source),target=participants.find(impact.targets[impact.next]);
    if(!source.id() || !target.id()) return DomainStatus::Stale;
    if(execution.melee && impact.weapon) {
        const auto function=impact.weapon->wearSkill.weapon->doFunction;
        if(function==67 || (function==78 && execution.hit)) {
            const int overlay=function==67?147:impact.missileDamage->formulas->skills.at(execution.skill).serverOverlay;
            if(!execution.terminalOverlayComplete) {
                if(overlay>0) {
                    const auto published=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},impact.area},
                        {OverlayFact{target.id(),uint8_t(target.player?0:1),impact.area,overlay}}});
                    if(!published) return published.status;
                }
                impact.nativeSkill->terminalOverlayComplete=true;
            }
            if(function==78) {
                const auto result=target.monster?ports_.effects.removeUnitState(target.id(),target.monster->rule.stunState,tick.tick):
                    ports_.effects.removeState({target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick},
                        target.player->rules.skills->stunned.id);
                if(!result) return result.status;
            }
        }
    }
    if(execution.psychicHammer) {
        const auto expected=source.player?source.player->combatRandom:source.monster->combatRandom;auto random=expected;
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        const auto stats=source.player?std::optional{source.player->totals.nativeStats}:ports_.effects.unitStatValues(source.id(),tick.tick);
        if(!stats) return DomainStatus::Unavailable;
        auto input=source.player?skills::formulaInput(*source.player,draw):skills::formulaInput(*source.monster,*stats,draw);
        SkillFormulaValues values(*impact.missileDamage->formulas,input);
        const auto field=target.player || (target.monster && target.monster->hireling)?"calc4":
            target.monster && target.monster->rule.boss?"calc3":
            target.monster && nativeUniqueMonster(target.monster->identity.rank)?"calc2":"calc1";
        const auto chance=values.calculation(FormulaDomain::Skill,execution.skill,std::max(execution.rank,1),field);
        if(!chance) throw std::logic_error("Unavailable native Psychic Hammer knockback formula");
        const bool knockback=*chance>0 && rollRandom(random)%100<uint32_t(*chance);
        if(random!=expected) {
            const auto committed=source.monster?ports_.monsters.consumeRandom(source.id(),expected,random).status:
                ports_.transactions.consumeRandom({source.player->player,source.id(),source.player->area,ports_.areas.at(source.player->area).generation,0,tick.tick},expected,random).status;
            if(committed!=DomainStatus::Applied) return committed;
        }
        impact.knockback|=knockback;
    }
    impact.nativeSkill->flagsComplete=true;return DomainStatus::Applied;
}
}
