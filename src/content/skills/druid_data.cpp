#include "druid_data.hpp"
#include "table_values.hpp"
#include "aura_data.hpp"
#include "content/classic_data.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/skills/druid_summon.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "resources/anim_data.hpp"
#include "content/monsters/monster_animation.hpp"
#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>

namespace d2x {
namespace {
DruidProgram procedure(int function) {
    switch(function) {
    case 0:return DruidProgram::Boulder;
    case 2:return DruidProgram::FireClaws;
    case 8:return DruidProgram::ShockWave;
    case 13:return DruidProgram::Fury;
    case 18:return DruidProgram::Defensive;
    case 19:return DruidProgram::Arctic;
    case 28:return DruidProgram::Fissure;
    case 114:return DruidProgram::Raven;
    case 115:return DruidProgram::Vine;
    case 116:return DruidProgram::Shape;
    case 117:return DruidProgram::Firestorm;
    case 118:return DruidProgram::Wind;
    case 119:return DruidProgram::Summon;
    case 120:return DruidProgram::Rage;
    case 121:return DruidProgram::Rabies;
    case 122:return DruidProgram::Hunger;
    case 123:return DruidProgram::Volcano;
    case 124:return DruidProgram::Periodic;
    default:throw std::runtime_error("Unsupported native druid procedure");
    }
}
SummonSkillSpec summonDefinition(const ClassicData &data,size_t skillRow) {
    const auto &skills=data.tables.at("skills"),&monsters=data.tables.at("monstats"),&extras=data.tables.at("monstats2");
    const auto number=[](const DataTable &table,size_t row,std::string_view field) {return skillTableNumber(table,row,field,SkillNumberPolicy::ColumnIsZero);};
    SummonSkillSpec spec;spec.corpse=false;spec.kind=MonsterKind::DruidPet;spec.monster=skills.value(skillRow,"summon");
    const auto row=findSkillTableRow(monsters,"Id",spec.monster),extra=findSkillTableRow(extras,"Id",monsters.value(row,"MonStatsEx"));
    spec.petType=number(data.tables.at("pettype"),findSkillTableRow(data.tables.at("pettype"),"pet type",skills.value(skillRow,"pettype")),"idx");
    if(number(monsters,row,"noRatio")!=1) throw std::runtime_error("Unsupported native druid pet ratios");
    auto pet=std::make_shared<DruidPetDefinition>();pet->nativeClass=number(monsters,row,"hcIdx");
    pet->group=number(data.tables.at("pettype"),findSkillTableRow(data.tables.at("pettype"),"pet type",skills.value(skillRow,"pettype")),"group");
    pet->summonMode=skills.value(skillRow,"summode");pet->attackable=number(extras,extra,"isAtt")!=0;
    const auto ai=monsters.value(row,"AI");
    if(ai=="Raven") pet->ai=DruidPetDefinition::Ai::Raven;
    else if(ai=="DruidWolf") pet->ai=spec.monster=="spiritwolf"?DruidPetDefinition::Ai::SpiritWolf:DruidPetDefinition::Ai::DireWolf;
    else if(ai=="Totem") pet->ai=DruidPetDefinition::Ai::Spirit;
    else if(ai=="Vines") {pet->ai=DruidPetDefinition::Ai::PoisonVine;pet->vine=true;}
    else if(ai=="CycleOfLife") {pet->ai=spec.monster=="cycleoflife"?DruidPetDefinition::Ai::LifeVine:DruidPetDefinition::Ai::ManaVine;pet->vine=true;}
    else if(ai=="DruidBear") pet->ai=DruidPetDefinition::Ai::Bear;
    else throw std::runtime_error("Missing native druid pet AI");
    for(size_t difficulty=0;difficulty<3;++difficulty) {
        const std::string suffix=difficulty==0?"":difficulty==1?"(N)":"(H)";
        auto &base=spec.base[difficulty];auto &stats=base.attributes;
        const int low=number(monsters,row,difficulty?"MinHP"+suffix:"minHP"),high=number(monsters,row,difficulty?"MaxHP"+suffix:"maxHP");
        if(low<1 || high<low) throw std::runtime_error("Missing native druid pet life interval");
        pet->lifeRanges[difficulty]={low,high};stats.maxLife=low;stats.maxLifeFixed=Fixed8(low);
        base.level=number(monsters,row,"Level"+suffix);stats.defense=number(monsters,row,"AC"+suffix);stats.attackRating=number(monsters,row,"A1TH"+suffix);
        base.minimumDamage=float(number(monsters,row,"A1MinD"+suffix));base.maximumDamage=float(number(monsters,row,"A1MaxD"+suffix));
        stats.combat.physicalResist=number(monsters,row,"ResDm"+suffix);stats.combat.magicResist=number(monsters,row,"ResMa"+suffix);
        stats.fireResist=number(monsters,row,"ResFi"+suffix);stats.lightningResist=number(monsters,row,"ResLi"+suffix);
        stats.coldResist=number(monsters,row,"ResCo"+suffix);stats.poisonResist=number(monsters,row,"ResPo"+suffix);
        const int velocity=number(monsters,row,"Velocity");stats.walkSpeed=float(velocity*256*75/100)*25.f/4096.f;
        base.followVelocityBonus=velocity?std::min(100,100*number(monsters,row,"Run")/velocity-100):0;
        base.block=number(monsters,row,"ToBlock"+suffix);base.collisionSize=number(extras,extra,"SizeX");
        base.monsterResistanceRules=true;base.damageRegen=number(monsters,row,"DamageRegen");base.critical=number(monsters,row,"Crit");base.drain=number(monsters,row,"Drain"+suffix);
        for(size_t index=0;index<8;++index) pet->parameters[index][difficulty]=number(monsters,row,"aip"+std::to_string(index+1)+suffix);
    }
    const auto &levels=data.tables.at("monlvl");
    for(size_t level=0;level<levels.rows().size();++level) {
        std::array<int,3> ac{},th{},lac{},lth{};
        for(size_t difficulty=0;difficulty<3;++difficulty) {
            const std::string suffix=difficulty==0?"":difficulty==1?"(N)":"(H)";
            ac[difficulty]=number(levels,level,"AC"+suffix);th[difficulty]=number(levels,level,"TH"+suffix);
            lac[difficulty]=number(levels,level,"L-AC"+suffix);lth[difficulty]=number(levels,level,"L-TH"+suffix);
        }
        spec.levelDefenseNonL.push_back(ac);spec.levelAttackNonL.push_back(th);spec.levelDefense.push_back(lac);spec.levelAttack.push_back(lth);
    }
    for(size_t index=0;index<8;++index) {
        std::string name(monsters.value(row,"Skill"+std::to_string(index+1)));
        for(auto &letter:name) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        if(!name.empty()) pet->skills[index]=data.nativeFormulas->skillIds.at(name);
        pet->skillModes[index]=monsters.value(row,"Sk"+std::to_string(index+1)+"mode");
    }
    spec.druid=std::move(pet);return spec;
}
}
void loadDruidSkills(ClassicData &data,Archives &archives) {
    auto auraPrograms=std::make_shared<std::map<int,AuraSkillSpec>>(*data.nativeAuraPrograms);
    const auto &skills=data.tables.at("skills"),&missiles=data.tables.at("missiles"),&states=data.tables.at("states");
    const AnimDataTable animations(archives.read("data/global/animdata.d2"));
    const auto &monsters=data.tables.at("monstats"),&extras=data.tables.at("monstats2");
    constexpr std::array modes{"dt","nu","wl","gh","a1","a2","bl","sc","s1","s2","s3","s4","dd","kb","sq","rn"};
    for(size_t row=0;row<states.rows().size();++row) {
        if(states.number(row,"transform").value_or(0)==0 || states.number(row,"gfxtype").value_or(-1)!=1) continue;
        const int nativeClass=skillTableNumber(states,row,"gfxclass");
        const auto monster=findSkillTableRow(monsters,"hcIdx",std::to_string(nativeClass));
        const auto extra=findSkillTableRow(extras,"Id",monsters.value(monster,"MonStatsEx"));
        ShapeProfile shape;shape.state=skillTableNumber(states,row,"id");shape.nativeClass=nativeClass;
        shape.restrictSkills=states.number(row,"restrict").value_or(0)!=0;shape.meleeOnly=states.number(row,"meleeonly").value_or(0)!=0;
        shape.token=monsters.value(monster,"Code");shape.weapon=extras.value(extra,"BaseW");
        for(auto &letter:shape.token) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        for(auto &letter:shape.weapon) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        shape.walkVelocity=skillTableNumber(monsters,monster,"Velocity");shape.runVelocity=skillTableNumber(monsters,monster,"Run");
        for(size_t mode=0;mode<modes.size();++mode) {
            if(!extras.number(extra,std::string("m")+modes[mode]).value_or(0)) continue;
            shape.enabledModes|=uint16_t{1}<<mode;
            const auto weapon=monsterModeWeapon(archives,shape.token,modes[mode],shape.weapon);
            auto key=shape.token+std::string(modes[mode])+weapon;
            for(auto &letter:key) letter=char(std::toupper(static_cast<unsigned char>(letter)));
            const auto *animation=animations.find(key);if(!animation || !animation->frames || animation->speed<=0) continue;
            int event=0;for(size_t frame=0;frame<std::min<size_t>(animation->frames,animation->frameFlags.size());++frame)
                if(animation->frameFlags[frame]==1 || animation->frameFlags[frame]==2) {event=int(frame);break;}
            if(animation->frames>animation->frameFlags.size()) throw std::runtime_error("Invalid native transformation frame count");
            shape.animations.emplace(uint8_t(mode),ShapeAnimation{int(animation->frames),animation->speed,event,
                {animation->frameFlags.begin(),animation->frameFlags.begin()+animation->frames}});
        }
        if(!shape.animations.contains(1) || !shape.animations.contains(4)) throw std::runtime_error("Missing native transformation animation");
        data.skills.shapes.emplace(shape.state,std::move(shape));
    }
    for(size_t row=0;row<skills.rows().size();++row) {
        if(skills.value(row,"charclass")!="dru") continue;
        const int id=skillTableNumber(skills,row,"Id");auto &entry=data.skills.skills.at(id);
        // Lycanthropy has no passive list: the two morph formulas query its rank.
        if(entry.passive) continue;
        auto native=std::make_shared<DruidSkillSpec>();
        native->startFunction=uint16_t(skills.number(row,"srvstfunc").value_or(0));native->doFunction=uint16_t(skills.number(row,"srvdofunc").value_or(0));
        native->program=procedure(native->doFunction);native->mode=skills.value(row,"anim");
        for(auto &letter:native->mode) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        native->sequence=skills.number(row,"seqnum").value_or(-1);native->restriction=skills.number(row,"restrict").value_or(0);
        native->useAttackRate=skills.number(row,"UseAttackRate").value_or(0)!=0;native->useManaOnDo=skills.number(row,"usemanaondo").value_or(0)!=0;
        const auto state=[&](std::string_view field) {const auto name=skills.value(row,field);return name.empty()?CombatStateDefinition{}:data.states.at(std::string(name)).definition;};
        native->state=state("aurastate");native->targetState=state("auratargetstate");
        for(size_t index=0;index<3;++index) {
            native->requiredStates[index]=state("State"+std::to_string(index+1)).id;
            const auto name=skills.value(row,std::array{"srvmissilea","srvmissileb","srvmissilec"}[index]);
            if(!name.empty()) native->missiles[index]=skillTableNumber(missiles,findSkillTableRow(missiles,"Missile",name),"Id");
        }
        if(native->state.id>=0) {
            const auto source=findSkillTableRow(states,"id",std::to_string(native->state.id));
            native->activeFunction=states.number(source,"srvactivefunc").value_or(0);
            if(native->program==DruidProgram::Shape) {
                native->transformClass=skillTableNumber(states,source,"gfxclass");
                for(size_t index=0;index<states.rows().size();++index)
                    if(states.number(index,"group").value_or(0)==native->state.group) native->removalGroupStates.push_back(skillTableNumber(states,index,"id"));
            }
        }
        SkillSpec spec=entry.spell?*entry.spell:SkillSpec{};spec.sourceId=id;spec.druid=std::move(native);
        spec.effect=spec.druid->program==DruidProgram::Defensive?SkillBehavior::NativeDefensiveBuff:SkillBehavior::Druid;
        spec.state=spec.druid->state;
        spec.mana=skillTableNumber(skills,row,"mana");spec.minimumMana=skillTableNumber(skills,row,"minmana");
        spec.manaPerLevel=skillTableNumber(skills,row,"lvlmana");spec.manaShift=skillTableNumber(skills,row,"manashift");
        spec.startMana=skills.number(row,"startmana").value_or(0);spec.hitShift=skills.number(row,"HitShift").value_or(0);spec.hitClass=skills.number(row,"HitClass").value_or(0);
        const auto element=skills.value(row,"EType");spec.fireDamage=element=="fire";spec.coldDamage=element=="cold";spec.poisonDamage=element=="pois";
        spec.physicalDamageCurve=element.empty();
        const auto main=skills.value(row,"srvmissile");spec.missileId=main.empty()?spec.druid->missiles[0]:skillTableNumber(missiles,findSkillTableRow(missiles,"Missile",main),"Id");
        const auto program=spec.druid->program;
        if(program==DruidProgram::Raven || program==DruidProgram::Vine || program==DruidProgram::Summon) {
            spec.summon=summonDefinition(data,row);
            for(int slot=1;slot<=5;++slot) {
                const auto name=skills.value(row,"sumskill"+std::to_string(slot));if(name.empty()) continue;
                const auto inherited=findSkillTableRow(skills,"skill",name);
                if(skills.number(inherited,"aura").value_or(0)) {
                    const int aura=skillTableNumber(skills,inherited,"Id");
                    auraPrograms->insert_or_assign(aura,prepareAura(data,aura));
                }
            }
        }
        if(program==DruidProgram::Rage || program==DruidProgram::Rabies || program==DruidProgram::FireClaws || program==DruidProgram::Hunger || program==DruidProgram::Fury) {
            spec.effect=SkillBehavior::WeaponProjectile;spec.weapon.emplace();spec.weapon->requiredType="mele";spec.weapon->mode=spec.druid->mode;
            spec.weapon->attackRating=skills.number(row,"ToHit").value_or(0);spec.weapon->attackRatingPerLevel=skills.number(row,"LevToHit").value_or(0);
            spec.weapon->interruptible=skills.number(row,"interrupt").value_or(0)!=0;
            if(program==DruidProgram::Fury) spec.weapon->rollbackPercent=skillTableNumber(skills,row,"Param2");
        }
        entry.spell=std::make_shared<const SkillSpec>(std::move(spec));
    }
    data.nativeAuraPrograms=std::move(auraPrograms);
}
}
