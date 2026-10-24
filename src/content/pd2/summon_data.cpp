#include "summon_data.hpp"
#include "skill_data.hpp"
#include "content/classic_data.hpp"
#include "content/skills/necro_summon_data.hpp"
#include "content/skills/missile_effects.hpp"
#include "content/skills/table_values.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/necro_summon_spec.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/skills/spec.hpp"
#include "resources/archive.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace d2x {
void preparePd2NecromancerSummons(ClassicData &data,Archives &archives) {
    const auto &skills=data.tables.at("skills"),&missiles=data.tables.at("missiles"),&pets=data.tables.at("pettype");
    for(auto &[id,record]:data.skills.skills) {
        if(record.classCode!="nec" || record.passive) continue;
        const auto row=findSkillTableRow(skills,"Id",std::to_string(id));
        const auto number=[&](std::string_view field) {return skillTableNumber(skills,row,field,SkillNumberPolicy::BlankIsZero);};
        const int action=number("srvdofunc"),start=number("srvstfunc");
        if((action==60 && !start) || (action==62 && start==19)) {
            auto spell=preparePd2SkillDefinition(data,row);spell.effect=SkillBehavior::BoneWall;
            const auto summon=prepareNativeNecromancerPet(data,std::string(skills.value(row,"summon")));
            auto barrier=std::make_shared<BoneSkillSpec>();barrier->barrier=true;barrier->prison=action==62;
            barrier->barrierStats=summon.base;spell.missileId=spell.pd2->missiles[0];
            spell.bone=std::move(barrier);record.spell=std::make_shared<const SkillSpec>(std::move(spell));continue;
        }
        const bool mage=action==31 && skills.value(row,"summon")=="necromage";
        const bool archer=action==31 && skills.value(row,"summon")=="necroarcher";
        const bool golem=(action==56 && start==74) || (action==57 && start==20);
        const bool revive=action==58 && start==21;
        if(!mage && !archer && !golem && !revive) continue;
        auto spell=preparePd2SkillDefinition(data,row);spell.effect=SkillBehavior::RaiseSkeleton;
        SummonSkillSpec summon;
        if(!revive) {
            std::string code(skills.value(row,"summon"));
            for(auto &letter:code) letter=char(std::tolower(static_cast<unsigned char>(letter)));
            summon=prepareNativeNecromancerPet(data,std::move(code));
        }
        summon.corpse=mage || archer || revive;summon.golem=golem;
        // The controlled NecroPet ranged role is shared. Native class,
        // components, missile, skills and all stats retain the Archer rows.
        if(archer) summon.kind=MonsterKind::NecroMage;
        auto program=std::make_shared<NecroSummonSpec>();
        program->kind=mage?NecroSummonKind::Mage:archer?NecroSummonKind::Archer:revive?NecroSummonKind::Revive:
            action==57?NecroSummonKind::Iron:summon.monster=="bloodgolem"?NecroSummonKind::Blood:
            summon.monster=="firegolem"?NecroSummonKind::Fire:NecroSummonKind::Clay;
        for(size_t slot=0;slot<program->parameters.size();++slot) program->parameters[slot]=number("Param"+std::to_string(slot+1));
        program->slowState=data.states.at("slowed").definition;
        if(mage) {
            const auto missileSkill=findSkillTableRow(skills,"skill",skills.value(row,"sumskill1"));
            if(skills.number(missileSkill,"srvdofunc")!=149) throw std::runtime_error("Unsupported PD2 skeleton mage attack");
            program->missileSkill=skillTableNumber(skills,missileSkill,"Id");
            const auto first=findSkillTableRow(missiles,"Missile",skills.value(missileSkill,"srvmissilea"));
            for(size_t element=0;element<program->missiles.size();++element) {
                const auto source=first+element;auto &definition=program->missiles[element];
                const auto value=[&](std::string_view field) {return skillTableNumber(missiles,source,field,SkillNumberPolicy::BlankIsZero);};
                const auto resource=loadProjectileResource(missiles,source,archives);
                definition.id=resource.id;definition.lifetime=resource.lifetime;
                definition.velocity=float(value("Vel")*256*75/100)*25.f/4096.f;
                definition.hitShift=value("HitShift");definition.maximum=value("MaxDamage");definition.frames=value("ELen");
                const auto type=missiles.value(source,"EType");
                definition.element=type=="pois"?DamageType::Poison:type=="cold"?DamageType::Cold:type=="fire"?DamageType::Fire:DamageType::Lightning;
                definition.minimumDamage.base=value("EMin");definition.maximumDamage.base=value("EMax");
                for(int step=0;step<5;++step) {
                    definition.minimumDamage.perLevel[step]=value("MinELev"+std::to_string(step+1));
                    definition.maximumDamage.perLevel[step]=value("MaxELev"+std::to_string(step+1));
                }
                for(int step=0;step<3;++step) definition.framesPerLevel[step]=value("ELevLen"+std::to_string(step+1));
                spell.submissileResources.push_back(resource);
                if(const auto name=missiles.value(source,"ExplosionMissile");!name.empty()) {
                    const auto impact=loadProjectileResource(missiles,findSkillTableRow(missiles,"Missile",name),archives);
                    definition.impact.visualId=impact.id;definition.impact.visualDuration=impact.lifetime;
                    spell.submissileResources.push_back(impact);
                }
            }
        }
        if(revive) {
            program->reviveState=data.states.at("revive").definition;
            for(size_t size=0;size<program->reviveVisuals.size();++size) {
                const auto name=skills.value(row,"cltmissile"+std::string(1,char('a'+size)));
                const auto resource=loadProjectileResource(missiles,findSkillTableRow(missiles,"Missile",name),archives);
                program->reviveVisuals[size]={resource.id,resource.lifetime};spell.submissileResources.push_back(resource);
            }
        }
        const auto pet=findSkillTableRow(pets,"pet type",skills.value(row,"pettype"));
        summon.petType=skillTableNumber(pets,pet,"idx");
        summon.iconArt="data/global/ui/hireables/"+std::string(pets.value(pet,"baseicon"))+".dc6";
        summon.necro=std::move(program);spell.summon=std::move(summon);
        record.spell=std::make_shared<const SkillSpec>(std::move(spell));
    }
}
}
