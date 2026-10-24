#include "assassin_data.hpp"
#include "table_values.hpp"
#include "missile_effects.hpp"
#include "content/classic_data.hpp"
#include "content/pd2/skill_data.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "resources/archive.hpp"
#include <cctype>
#include <memory>
#include <stdexcept>

namespace d2x {
namespace {
SummonSkillSpec summonDefinition(const ClassicData &data,const AssassinSkillSpec &skill,size_t skillRow) {
    const auto &monstats=data.tables.at("monstats"),&extras=data.tables.at("monstats2"),&levels=data.tables.at("monlvl");
    const auto row=findSkillTableRow(monstats,"Id",skill.summon);
    const auto extra=findSkillTableRow(extras,"Id",monstats.value(row,"MonStatsEx"));
    if(monstats.number(row,"noRatio")!=1) throw std::runtime_error("Unsupported native assassin pet ratio");
    SummonSkillSpec spec;spec.corpse=false;spec.monster=skill.summon;spec.kind=MonsterKind::AssassinPet;spec.petType=skill.petType;
    auto pet=std::make_shared<AssassinPetDefinition>();pet->nativeClass=skill.summonClass;
    const auto ai=monstats.value(row,"AI");
    if(ai=="AssassinSentry") pet->ai=AssassinPetDefinition::Ai::Sentry;
    else if(ai=="BladeCreeper") pet->ai=AssassinPetDefinition::Ai::BladeCreeper;
    else if(ai=="DeathSentry") pet->ai=AssassinPetDefinition::Ai::DeathSentry;
    else if(ai=="ShadowWarrior") pet->ai=AssassinPetDefinition::Ai::ShadowWarrior;
    else if(ai=="ShadowMaster") pet->ai=AssassinPetDefinition::Ai::ShadowMaster;
    else throw std::runtime_error("Missing native assassin pet AI");
    pet->summonMode=data.tables.at("skills").value(skillRow,"summode");
    pet->attackable=extras.number(extra,"isAtt").value_or(0)!=0;
    pet->infernoFrames=extras.number(extra,"InfernoLen").value_or(0);pet->infernoAnimation=extras.number(extra,"InfernoAnim").value_or(0);
    pet->warp=pet->ai==AssassinPetDefinition::Ai::ShadowWarrior || pet->ai==AssassinPetDefinition::Ai::ShadowMaster;
    const auto number=[&](const DataTable &table,size_t r,std::string_view name) {return skillTableNumber(table,r,name,SkillNumberPolicy::ColumnIsZero);};
    for(size_t difficulty=0;difficulty<3;++difficulty) {
        const std::string suffix=difficulty==0?"":difficulty==1?"(N)":"(H)";
        auto &stats=spec.base[difficulty];auto &a=stats.attributes;
        a.maxLife=number(monstats,row,difficulty?"MinHP"+suffix:"minHP");a.maxLifeFixed=Fixed8(a.maxLife);
        pet->lifeRanges[difficulty]={a.maxLife,number(monstats,row,difficulty?"MaxHP"+suffix:"maxHP")};
        stats.level=number(monstats,row,"Level"+suffix);a.defense=number(monstats,row,"AC"+suffix);a.attackRating=number(monstats,row,"A1TH"+suffix);
        stats.minimumDamage=float(number(monstats,row,"A1MinD"+suffix));stats.maximumDamage=float(number(monstats,row,"A1MaxD"+suffix));
        a.combat.physicalResist=number(monstats,row,"ResDm"+suffix);a.combat.magicResist=number(monstats,row,"ResMa"+suffix);
        a.fireResist=number(monstats,row,"ResFi"+suffix);a.lightningResist=number(monstats,row,"ResLi"+suffix);
        a.coldResist=number(monstats,row,"ResCo"+suffix);a.poisonResist=number(monstats,row,"ResPo"+suffix);
        a.walkSpeed=float(number(monstats,row,"Velocity")*256*75/100)*25.f/4096.f;
        const int velocity=number(monstats,row,"Velocity");stats.followVelocityBonus=velocity?std::min(100,100*number(monstats,row,"Run")/velocity-100):0;
        stats.monsterResistanceRules=true;stats.block=number(monstats,row,"ToBlock"+suffix);stats.collisionSize=number(extras,extra,"SizeX");
        stats.damageRegen=number(monstats,row,"DamageRegen");stats.critical=number(monstats,row,"Crit");stats.drain=number(monstats,row,"Drain"+suffix);
        for(int index=0;index<8;++index) pet->parameters[size_t(index)][difficulty]=number(monstats,row,"aip"+std::to_string(index+1)+suffix);
    }
    for(size_t level=0;level<levels.rows().size();++level) {
        std::array<int,3> ac{},th{},lac{},lth{};
        for(size_t difficulty=0;difficulty<3;++difficulty) {
            const std::string suffix=difficulty==0?"":difficulty==1?"(N)":"(H)";
            ac[difficulty]=number(levels,level,"AC"+suffix);th[difficulty]=number(levels,level,"TH"+suffix);
            lac[difficulty]=number(levels,level,"L-AC"+suffix);lth[difficulty]=number(levels,level,"L-TH"+suffix);
        }
        spec.levelDefenseNonL.push_back(ac);spec.levelAttackNonL.push_back(th);spec.levelDefense.push_back(lac);spec.levelAttack.push_back(lth);
    }
    pet->skills.fill(-1);
    for(int index=0;index<8;++index) {
        auto name=std::string(monstats.value(row,"Skill"+std::to_string(index+1)));
        if(name.empty()) continue;
        for(auto &c:name) c=char(std::tolower(static_cast<unsigned char>(c)));
        pet->skills[size_t(index)]=data.nativeFormulas->skillIds.at(name);
    }
    spec.assassin=std::move(pet);return spec;
}
AssassinProgram program(int function) {
    switch(function) {
    case 0:return AssassinProgram::FireBlast;
    case 18:return AssassinProgram::Defensive;
    case 33:return AssassinProgram::PsychicHammer;
    case 34:return AssassinProgram::Charge;
    case 35:return AssassinProgram::ElementalCharge;
    case 42:return AssassinProgram::DragonTalon;
    case 43:return AssassinProgram::ShockWeb;
    case 44:return AssassinProgram::BladeSentinel;
    case 45:return AssassinProgram::Sentry;
    case 46:return AssassinProgram::DragonClaw;
    case 47:return AssassinProgram::Cloak;
    case 48:return AssassinProgram::BladeFury;
    case 49:return AssassinProgram::Shadow;
    case 50:return AssassinProgram::DragonTail;
    case 51:return AssassinProgram::MindBlast;
    case 52:return AssassinProgram::DragonFlight;
    case 178:return AssassinProgram::DragonFlight;
    case 54:return AssassinProgram::BladeShield;
    default:throw std::runtime_error("Unsupported original assassin skill program");
    }
}
}
void loadAssassinSkills(ClassicData &data,Archives &archives) {
    const auto &skills=data.tables.at("skills"),&missiles=data.tables.at("missiles");
    const auto &monsters=data.tables.at("monstats"),&pets=data.tables.at("pettype");
    for(size_t row=0;row<skills.rows().size();++row) {
        const bool amazon=data.pd2Host && skills.value(row,"charclass")=="ama" &&
            (skills.number(row,"srvdofunc")==16 || skills.number(row,"srvdofunc")==49);
        if(skills.value(row,"charclass")!="ass" && !amazon) continue;
        const auto id=skillTableNumber(skills,row,"Id");
        if(data.pd2Host && !data.skills.skills.contains(id)) continue;
        auto &entry=data.skills.skills.at(id);
        // Both passives use the native five-slot refresh and parameter layers.
        if(entry.passive) {
            if(data.nativeFormulas->skills.at(id).passiveState<0)
                throw std::runtime_error("Missing original assassin passive state");
            continue;
        }
        auto native=std::make_shared<AssassinSkillSpec>();
        native->startFunction=uint16_t(skills.number(row,"srvstfunc").value_or(0));
        native->doFunction=uint16_t(skills.number(row,"srvdofunc").value_or(0));
        if(data.pd2Host && !amazon && native->doFunction!=42 && native->doFunction!=43 && native->doFunction!=44 && native->doFunction!=45 &&
           native->doFunction!=46 && native->doFunction!=47 && native->doFunction!=48 && native->doFunction!=49 &&
           native->doFunction!=50 && native->doFunction!=51 && native->doFunction!=54 && native->doFunction!=170 && native->doFunction!=178) continue;
        native->program=amazon?AssassinProgram::Shadow:native->doFunction==170?(skills.value(row,"anim")=="A2"?AssassinProgram::ElementalCharge:AssassinProgram::Charge):program(native->doFunction);
        native->mode=skills.value(row,"anim");
        for(auto &letter:native->mode) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        native->sequence=skills.number(row,"seqnum").value_or(-1);
        native->useAttackRate=skills.number(row,"UseAttackRate").value_or(0)!=0;
        native->useManaOnDo=skills.number(row,"usemanaondo").value_or(0)!=0;
        const auto state=[&](std::string_view field) {
            const auto name=skills.value(row,field);if(name.empty()) return CombatStateDefinition{};
            const auto found=data.states.find(name);
            if(found==data.states.end()) throw std::runtime_error("Missing original assassin state");
            return found->second.definition;
        };
        native->state=state("aurastate");native->targetState=state("auratargetstate");
        if(amazon && native->doFunction==16) native->state=data.states.at("valkyrie").definition;
        if(native->program==AssassinProgram::MindBlast) {
            native->conversionState=data.states.at("conversion").definition;
            native->conversionSaveState=data.states.at("conversion_save").definition;
        }
        if(const auto name=skills.value(row,"tgtoverlay");!name.empty())
            native->targetOverlay=int(findSkillTableRow(data.tables.at("overlay"),"overlay",name));
        native->progressiveStack=skills.number(row,"prgstack").value_or(0)!=0;
        native->progressiveDamage=skills.number(row,"prgdam").value_or(0);
        const auto stat=[&](std::string_view field) {
            const auto name=skills.value(row,field);if(name.empty()) return -1;
            const auto found=data.nativeStats->names.find(name);
            if(found==data.nativeStats->names.end()) throw std::runtime_error("Missing original assassin stat");
            return int(found->second);
        };
        if(skills.number(row,"progressive").value_or(0)) {
            native->chargeStat=stat("aurastat1");native->chargeHitStat=stat("aurastat2");
            if(native->chargeStat<0 || native->state.id<0) throw std::runtime_error("Missing progressive list definition");
        }
        for(size_t index=0;index<3;++index) {
            native->progressiveFunctions[index]=uint16_t(skills.number(row,"srvprgfunc"+std::to_string(index+1)).value_or(0));
            const auto name=skills.value(row,std::array{"srvmissilea","srvmissileb","srvmissilec"}[index]);
            if(!name.empty()) native->missiles[index]=skillTableNumber(missiles,findSkillTableRow(missiles,"Missile",name),"Id");
        }
        native->summon=skills.value(row,"summon");
        if(!native->summon.empty()) {
            native->summonClass=skillTableNumber(monsters,findSkillTableRow(monsters,"Id",native->summon),"hcIdx");
            native->petType=skillTableNumber(pets,findSkillTableRow(pets,"pet type",skills.value(row,"pettype")),"idx");
        }
        SkillSpec spec=data.pd2Host?preparePd2SkillDefinition(data,row):entry.spell?*entry.spell:SkillSpec{};
        spec.assassin=std::move(native);spec.sourceId=id;
        if(spec.assassin->program!=AssassinProgram::Defensive) spec.effect=SkillBehavior::Assassin;
        spec.mana=skillTableNumber(skills,row,"mana");spec.minimumMana=skillTableNumber(skills,row,"minmana");
        spec.manaPerLevel=skillTableNumber(skills,row,"lvlmana");spec.manaShift=skillTableNumber(skills,row,"manashift");
        spec.startMana=skills.number(row,"startmana").value_or(0);
        spec.hitShift=skills.number(row,"HitShift").value_or(0);spec.hitClass=skills.number(row,"HitClass").value_or(0);
        spec.state=spec.assassin->state;const auto main=skills.value(row,"srvmissile");
        const auto role=spec.assassin->program;
        if(role==AssassinProgram::Sentry || role==AssassinProgram::BladeSentinel || role==AssassinProgram::Shadow) {
            spec.summon=summonDefinition(data,*spec.assassin,row);
            if(amazon) {
                auto definition=std::make_shared<AssassinPetDefinition>(*spec.summon->assassin);
                definition->inheritOwnerSkills=false;definition->summonFunction=spec.assassin->doFunction;
                definition->transformClass=spec.assassin->doFunction==16?"ama":"";
                spec.summon->assassin=std::move(definition);
            }
        }
        if(role==AssassinProgram::Charge || role==AssassinProgram::ElementalCharge || role==AssassinProgram::DragonClaw) {
            spec.effect=SkillBehavior::WeaponProjectile;spec.weapon.emplace();
            spec.weapon->requiredType=skills.value(row,"itypea1");
            spec.weapon->mode=role==AssassinProgram::ElementalCharge || role==AssassinProgram::DragonClaw?"a2":"a1";
            spec.weapon->sequence=spec.assassin->sequence;
            spec.weapon->dualWield=role==AssassinProgram::DragonClaw;
            spec.weapon->attackRating=skills.number(row,"ToHit").value_or(0);
            spec.weapon->attackRatingPerLevel=skills.number(row,"LevToHit").value_or(0);
            if(data.pd2Host && spec.assassin->startFunction==78)
                spec.weapon->attackState=data.states.at("temp_skill_buff").definition;
        }
        if(role==AssassinProgram::DragonTalon || role==AssassinProgram::DragonTail || role==AssassinProgram::DragonFlight) {
            spec.weapon.emplace();spec.weapon->mode="kk";spec.weapon->sequence=spec.assassin->sequence;
            spec.weapon->attackRating=skills.number(row,"ToHit").value_or(0);
            spec.weapon->attackRatingPerLevel=skills.number(row,"LevToHit").value_or(0);
            if(role!=AssassinProgram::DragonTail) {
                spec.weapon->damagePercent=skills.number(row,"Param1").value_or(0);
                spec.weapon->damagePerLevel=skills.number(row,"Param2").value_or(0);
            }
        }
        spec.missileId=main.empty()?spec.assassin->missiles[0]:skillTableNumber(missiles,findSkillTableRow(missiles,"Missile",main),"Id");
        if(spec.missileId>=0) {
            const auto source=findSkillTableRow(missiles,"Id",std::to_string(spec.missileId));
            const auto art=missiles.value(source,"CelFile");
            if(!art.empty() && art!="null") spec.missileArt=loadProjectileResource(missiles,source,archives).art;
            spec.missileVelocity=float(missiles.number(source,"Vel").value_or(0))*25.f/32.f;
            spec.missileLifetime=float(missiles.number(source,"Range").value_or(0))/25.f;
            spec.missileVelocityPerLevel=missiles.number(source,"VelLev").value_or(0);
            spec.missileRangePerLevel=missiles.number(source,"LevRange").value_or(0);
        }
        entry.spell=std::make_shared<const SkillSpec>(std::move(spec));
    }
}
}
