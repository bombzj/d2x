#include "system.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/actions/state.hpp"
#include "gameplay/combat/weapon_snapshot.hpp"
#include "gameplay/missiles/damage_data.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/stats/pd2/item_values.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace d2x::server::missiles {
namespace {
void programFields(MissileRule &rule,const SkillFormulaCatalog &catalog) {
    const auto &source=catalog.missiles.at(rule.definition);
    rule.sourceDamage=uint8_t(source.number("srcdamage"));rule.originSourceDamage=uint8_t(source.number("srcmissdmg"));
    rule.holy=uint8_t(source.number("holy"));rule.useMissileSkill=source.number("missileskill")!=0;
    rule.halfTwoHanded=source.number("half2hsrc")!=0;rule.applyMastery=source.number("applymastery")!=0;
    rule.damageElement=MissileDamageElement(source.elementType);
    if(const auto skill=source.reference("skill");!skill.empty()) rule.linkedSkill=catalog.skillIds.at(std::string(skill));
    rule.killOnHit=source.number("collidekill")!=0;rule.pierce=source.number("pierce")!=0;
    rule.collideFriend=source.number("collidefriend")!=0;rule.collisionMode=uint8_t(source.number("collidetype"));
    rule.lastCollide=source.number("lastcollide")!=0;
    rule.explosion=source.number("explosion")!=0;
    rule.activate=source.number("activate");
    rule.town=source.number("town")!=0;rule.sourceTown=source.number("srctown")!=0;
    rule.toHit=source.number("tohit")!=0;rule.returnFire=source.number("returnfire")!=0;
    rule.getHit=source.number("gethit")!=0;rule.softHit=source.number("softhit")!=0;
    rule.knockbackChance=uint8_t(source.number("knockback"));rule.hitClass=source.number("hitclass");
    rule.serverDoFunction=uint16_t(source.number("psrvdofunc"));rule.serverHitFunction=uint16_t(source.number("psrvhitfunc"));
    for(size_t slot=0;slot<rule.serverParameters.size();++slot) rule.serverParameters[slot]=source.number("param"+std::to_string(slot+1));
    for(size_t slot=0;slot<rule.hitParameters.size();++slot) rule.hitParameters[slot]=source.number("hitpar"+std::to_string(slot+1));
    rule.alwaysExplode=source.number("alwaysexplode")!=0;
    rule.serverDamageFunction=uint16_t(source.number("psrvdmgfunc"));
    rule.damageParameters={source.number("dmgparam1"),source.number("dmgparam2")};rule.programOverlay=source.procOverlay;
    rule.nextHit=source.number("nexthit")!=0;rule.nextDelay=rule.nextHit?source.number("nextdelay"):0;
    rule.canDestroy=source.number("candestroy")!=0;
    rule.clientSend=source.number("clientsend")!=0;
    rule.frames=nativeAdd(source.number("range"),nativeMultiply(rule.rank,source.number("levrange")));
    rule.baseVelocity=source.number("vel");rule.levelVelocity=source.number("vellev");
    const auto velocity=missileVelocityFixed(rule.baseVelocity,rule.levelVelocity,rule.rank);
    if(!velocity) throw std::runtime_error("Invalid native child missile velocity");
    rule.speed=float(*velocity)*25.f/4096.f;
    rule.acceleration=float(source.number("accel"))*25.f/4096.f;
    rule.maximumVelocity=float(source.number("maxvel"))*25.f/4096.f;
    rule.canSlow=source.number("canslow")!=0;
    constexpr uint16_t masks[]{0,0x84,0x104,0x184,0,0x104,4,0x40,0x185};
    if(rule.collisionMode>=std::size(masks)) throw std::runtime_error("Invalid native child collision mode");
    rule.collision={masks[rule.collisionMode],source.number("size")};
    rule.collidePlayers=rule.collisionMode==1 || rule.collisionMode==3 || rule.collisionMode==8;
    rule.collideMonsters=rule.collisionMode==2 || rule.collisionMode==3 || rule.collisionMode==5 || rule.collisionMode==8;
    const auto child=[&](std::string_view field) {
        const auto name=source.reference(field);if(name.empty()) return -1;
        return catalog.missileIds.at(std::string(name));
    };
    for(size_t slot=0;slot<rule.subMissiles.size();++slot) rule.subMissiles[slot]=child("submissile"+std::to_string(slot+1));
    for(size_t slot=0;slot<rule.hitSubMissiles.size();++slot) rule.hitSubMissiles[slot]=child("hitsubmissile"+std::to_string(slot+1));
}
}
MissileRule System::nativeDamageRule(const SkillRules &rules,int definition,int rank) const {
    if(!rules.formulas || !rules.stats) throw std::runtime_error("Missing native missile definitions");
    MissileRule rule;rule.definition=definition;rule.rank=rank;
    rule.damageStats=rules.stats;rule.damageFormulas=rules.formulas;rule.damageTypes=rules.monsterTypes;
    programFields(rule,*rules.formulas);
    rule.nextHitState=rules.justHit;rule.uninterruptableState=rules.uninterruptable.id;
    rule.lifeStealOverlay=rules.lifeStealOverlay;rule.manaStealOverlay=rules.manaStealOverlay;
    rule.lifeStealDivisor=rules.lifeStealDivisor;rule.manaStealDivisor=rules.manaStealDivisor;
    rule.aiCurseDivisor=rules.aiCurseDivisor;
    return rule;
}
MissileRule System::nativeChildRule(const Missile &parent,int definition) const {
    const auto &origin=parent.nativeDamage->rule;
    MissileRule rule;rule.definition=definition;rule.rank=parent.skill.rank;
    rule.damageStats=origin.damageStats;rule.damageFormulas=origin.damageFormulas;rule.damageTypes=origin.damageTypes;
    rule.nextHitState=origin.nextHitState;rule.uninterruptableState=origin.uninterruptableState;
    rule.lifeStealOverlay=origin.lifeStealOverlay;rule.manaStealOverlay=origin.manaStealOverlay;
    rule.lifeStealDivisor=origin.lifeStealDivisor;rule.manaStealDivisor=origin.manaStealDivisor;
    rule.aiCurseDivisor=origin.aiCurseDivisor;
    programFields(rule,*rule.damageFormulas);
    return rule;
}
void System::initializePierce(Missile &missile) const {
    missile.pierces=missile.pierceCount=0;if(!missile.nativeDamage || !missile.nativeDamage->rule.pierce) return;
    const auto &rule=missile.nativeDamage->rule;const auto *player=ports_.players.findActor(missile.owner);const auto *monster=ports_.monsters.find(missile.owner);
    const auto values=ports_.effects.unitStatValues(missile.owner,missile.created);
    const auto *base=player?player->totals.statSources.get():monster && monster->nativeStats?monster->nativeStats->sources.get():nullptr;
    if(!values || !base || !rule.damageStats) throw std::logic_error("Missing native missile pierce sources");
    const auto &stats=*rule.damageStats;
    const auto chance=nativeAdd(values->get({stats.names.at("skill_pierce"),0}),values->get({stats.names.at("item_pierce"),0}));
    const auto index=baseStatValue(stats,*base,{stats.names.at("pierce_idx"),0});
    if(chance) missile.pierces=missilePierceCount(chance,uint32_t(index));
}
std::shared_ptr<MissileDamageRollState> System::damageRolls(const Missile &missile) const {
    if(!missile.nativeDamage) return {};
    const auto *data=&missile.nativeDamage->data;const auto &rule=missile.nativeDamage->rule;
    const auto &bonuses=missile.nativeDamage->monsterTypeBonuses;
    auto result=std::make_shared<MissileDamageRollState>(MissileDamageRollState{*data,{},missile.random,bonuses,rule.damageTypes,
        rule.lifeStealOverlay,rule.manaStealOverlay,rule.lifeStealDivisor,rule.manaStealDivisor});
    result->statRules=rule.damageStats;result->formulas=rule.damageFormulas;
    result->definition=missile.definition;result->skill=missile.skill.sourceId;result->level=missile.skill.rank;
    result->serverDamageFunction=rule.serverDamageFunction;result->element=rule.damageElement;
    result->damageParameters=rule.damageParameters;result->programOverlay=rule.programOverlay;
    result->getHit=rule.getHit;result->softHit=rule.softHit;result->knockbackChance=rule.knockbackChance;
    result->uninterruptableState=rule.uninterruptableState;result->targetParameterX=missile.parameters[0];
    result->pierceCount=missile.pierceCount;
    return result;
}
MissileDamageData System::prepareDamage(const Missile &missile,const MissileRule &rule,std::shared_ptr<const StatList> mode,
    uint64_t &ownerRandom,bool &ownerDrawn,const Missile *origin,std::optional<std::reference_wrapper<uint64_t>> originRandom,std::vector<std::pair<uint16_t,int32_t>> *typeBonuses) const {
    if(!rule.damageStats || !rule.damageFormulas) throw std::runtime_error("Missing native missile damage definitions");
    const auto &stats=*rule.damageStats;const auto &catalog=*rule.damageFormulas;
    const auto *monster=ports_.monsters.find(missile.owner);const auto *player=ports_.players.findActor(missile.owner);
    StatSnapshot owner;
    std::optional<WeaponDamage> weapon;
    if(monster) {
        const auto sampled=ports_.effects.unitStatValues(monster->id,missile.created,
            monster->petWeapon && monster->petWeapon->item?std::optional{monster->petWeapon->item}:std::nullopt,mode);
        if(!sampled) throw std::runtime_error("Missing native missile owner stat source");
        owner=*sampled;
        if(monster->petWeapon && monster->petWeapon->item) weapon=monster->petWeapon;
        // Current monster admission still carries these verified UMod base
        // contributions in its prepared rule; temporary aura ED is in FullStats.
        if(monster->rule.enchantment) {
            const StatKey key{stats.names.at("damagepercent"),0};
            owner.values[key]=nativeAdd(owner.get(key),monster->rule.enchantment->damagePercent);
        }
    } else if(player) {
        const auto selected=missile.weapon?missile.weapon->weapon.item:player->totals.equipment.weapons[0].item;
        const auto sampled=ports_.effects.unitStatValues(player->actor,missile.created,selected?std::optional{selected}:std::nullopt,mode);
        if(!sampled) throw std::runtime_error("Missing native player missile owner stat source");
        owner=*sampled;
        if(missile.weapon) weapon=missile.weapon->weapon;
        else if(player->totals.equipment.weapons[0].item) weapon=player->totals.equipment.weapons[0];
    } else if(!missile.environment) throw std::runtime_error("Missing native missile owner");
    const auto read=[&](std::string_view name){return owner.get({stats.names.at(std::string(name)),0});};
    const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
        if(low>=high) return low;
        ownerDrawn=true;
        return nativeAdd(low,int32_t(limitedRandom(ownerRandom,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    };
    const bool linked=rule.useMissileSkill || rule.linkedSkill>0;
    const int skill=rule.useMissileSkill?missile.skill.sourceId:rule.linkedSkill;
    auto physical=MissileDamageRange{rule.minimum,rule.maximum};
    auto elemental=MissileDamageRange{rule.elementalMinimum,rule.elementalMaximum};
    auto element=rule.damageElement;int32_t length=rule.elementLength;
    int sourceDamage=rule.sourceDamage,originDamage=rule.originSourceDamage;
    if(linked) {
        if(!monster && !player) throw std::runtime_error("Native linked-skill missile requires its actual owner formula context");
        auto input=monster?skills::formulaInput(*monster,owner,draw):skills::formulaInput(*player,draw);
        SkillFormulaValues values(catalog,input);const auto rank=missile.skill.rank;
        const auto base=values.physicalRange(skill,rank,false);
        const auto low=values.parameter(FormulaDomain::Skill,skill,rank,"enms"),high=values.parameter(FormulaDomain::Skill,skill,rank,"exms");
        const auto duration=values.parameter(FormulaDomain::Skill,skill,rank,"edln");
        if(!base || !low || !high || !duration) throw std::runtime_error("Unavailable native missile skill damage input");
        physical={base->first,base->second};elemental={*low,*high};element=MissileDamageElement(catalog.skills.at(skill).elementType);length=*duration;
        sourceDamage=rule.sourceDamage==255?0:catalog.skills.at(skill).number("srcdam");originDamage=0;
    } else {
        if(!monster && !player) throw std::runtime_error("Native missile requires an owner formula context");
        auto input=monster?skills::formulaInput(*monster,owner,draw):skills::formulaInput(*player,draw);
        SkillFormulaValues values(catalog,input);
        const auto low=values.parameter(FormulaDomain::Missile,missile.definition,missile.skill.rank,"dmns");
        const auto high=values.parameter(FormulaDomain::Missile,missile.definition,missile.skill.rank,"dmxs");
        const auto emin=values.parameter(FormulaDomain::Missile,missile.definition,missile.skill.rank,"edns");
        const auto emax=values.parameter(FormulaDomain::Missile,missile.definition,missile.skill.rank,"edxs");
        const auto duration=values.parameter(FormulaDomain::Missile,missile.definition,missile.skill.rank,"edln");
        if(!low || !high || !emin || !emax || !duration) throw std::runtime_error("Unavailable native missile range context");
        physical={*low,*high};elemental={*emin,*emax};length=*duration;
    }
    if(!linked && rule.applyMastery) {
        std::string_view name;
        switch(element) {
        case MissileDamageElement::Fire:name="passive_fire_mastery";break;
        case MissileDamageElement::Lightning:name="passive_ltng_mastery";break;
        case MissileDamageElement::Cold:case MissileDamageElement::Freeze:name="passive_cold_mastery";break;
        case MissileDamageElement::Poison:name="passive_pois_mastery";break;
        default:break;
        }
        if(!name.empty()) {
            const auto mastery=read(name);
            elemental.minimum=nativeAdd(elemental.minimum,nativePercentage(elemental.minimum,mastery,100));
            elemental.maximum=nativeAdd(elemental.maximum,nativePercentage(elemental.maximum,mastery,100));
        }
    }
    bool bonus{};std::optional<MissileDamageWeapon> equipment;
    if(sourceDamage) {
        if(!monster && !player) throw std::runtime_error("Native source damage requires owner attributes");
        int mastery{};
        if(weapon) {
            const auto *gear=player?&player->totals.equipment:monster->equipmentStats?&*monster->equipmentStats:nullptr;
            const auto dual=player?(player->definition.code=="bar" || player->definition.code=="ass"):
                monster->identity.monster=="shadowwarrior" || monster->identity.monster=="shadowmaster";
            const auto sampled=sampleNativeWeapon(*weapon,owner,stats,monster?monster->petStats.attributes.combat:player->totals.character.combat,bool(monster),0,gear,&catalog,dual);
            weapon=sampled;
            const auto used=ports_.actions.usedSkill(missile.owner);
            const auto current=used?catalog.skills.find(used->skill):catalog.skills.end();
            const bool throwing=current!=catalog.skills.end() && current->second.useThrowMastery;
            const auto &selected=throwing?sampled.throwMastery:sampled.meleeMastery;mastery=selected[2];
            equipment=MissileDamageWeapon{sampled.twoHanded,sampled.throwable,player && player->totals.equipment.weaponCount==2,
                sampled.blunt,int16_t(sampled.strengthBonus),int16_t(sampled.dexterityBonus),selected[1]};
        }
        if(stats.pd2) {
            const auto used=ports_.actions.usedSkill(missile.owner);
            const auto current=used?catalog.skills.find(used->skill):catalog.skills.end();
            if(current!=catalog.skills.end() && (current->second.number("srcdam") || used->skill==240 || used->skill==245 || used->skill==243))
                bonus=rollPd2Critical(pd2ItemModifiers(owner,stats),read("passive_critical_strike"),mastery,read("item_deadlystrike"),ownerRandom).kind!=0;
        } else bonus=missileHasBonusStats(read("passive_critical_strike"),read("item_deadlystrike"),bool(weapon),mastery,ownerRandom);
        ownerDrawn=true;
    }
    StatSnapshot originStats;
    const auto *originData=origin && origin->nativeDamage?&origin->nativeDamage->data:nullptr;
    if(originData) {
        for(const auto &write:missileDamageStatWrites(*originData,origin->skill.rank,owner,stats)) originStats.values.insert_or_assign(write.key,write.value);
    }
    if(!sourceDamage && originDamage && origin) {
        const auto critical=originStats.get({stats.names.at("passive_critical_strike"),0});
        const auto deadly=originStats.get({stats.names.at("item_deadlystrike"),0});
        if(!originRandom) throw std::runtime_error("Missing native origin missile seed loan");
        if(!stats.pd2) bonus=missileHasBonusStats(critical,deadly,false,0,originRandom->get());
    }
    if(typeBonuses) {
        typeBonuses->clear();const auto id=stats.names.at("damage_vs_montype");
        for(const auto &[key,value]:owner.values) if(key.id==id && typeBonuses->size()<128) typeBonuses->emplace_back(key.layer,value);
    }
    StatSnapshot initial;
    if(missile.initialDamagePercent) initial.values.emplace(StatKey{stats.names.at("damagepercent"),0},missile.initialDamagePercent);
    const MissileDamageInput input{.physical=physical,.elemental=elemental,.element=element,.length=length,
        .unitSourceDamage=sourceDamage,.originSourceDamage=originDamage,.holy=rule.holy,.halfTwoHanded=rule.halfTwoHanded,
        .hireling=monster && monster->hireling,.bonusStats=bonus,.weapon=equipment,.owner=owner,.missile=initial,.origin=origin?&originStats:nullptr};
    return calculateMissileDamageData(input,stats);
}
}
