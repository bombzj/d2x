#include "hirelings.hpp"
#include "hosting/monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/pd2/skill_data.hpp"
#include "content/monsters/monster_animation.hpp"
#include "content/skills/aura_data.hpp"
#include "content/skills/table_values.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/companions/preparation.hpp"
#include "server/systems/companions/hireling_equipment.hpp"
#include "resources/archive.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace d2x::hosting::pd2 {
void prepareHirelingSkills(const ClassicData &data,server::SkillRules &rules) {
    const auto &skills=data.tables.at("skills");
    for(const auto &hireling:data.hirelings) for(const auto &skill:hireling.skills) {
        if(rules.definitions.contains(skill.id)) continue;
        const auto *record=data.skills.find(skill.id);
        const auto row=findSkillTableRow(skills,"Id",std::to_string(skill.id));
        auto spec=record && record->spell?*record->spell:preparePd2SkillDefinition(data,row);
        if(spec.effect==SkillBehavior::None) spec.effect=SkillBehavior::Pd2;
        if(spec.pd2->startFunction==4 || spec.pd2->startFunction==8) {
            auto &weapon=spec.weapon.emplace();weapon.mode="a1";weapon.noAmmo=true;
            if(spec.pd2->startFunction==8) {auto bow=std::make_shared<BowSkillSpec>();bow->strafe=true;weapon.bow=std::move(bow);}
        }
        if(spec.pd2->doFunction==160) {
            spec.effect=SkillBehavior::StaticField;spec.curse.emplace();
            spec.curse->state=data.states.at(std::string(skills.value(row,"auratargetstate"))).definition;
            spec.curse->targetFilter=skills.number(row,"aurafilter").value_or(0);
        }
        rules.definitions.emplace(skill.id,server::SkillDefinition{spec.rules(),
            skills.number(row,"InTown").value_or(0)!=0,{}});
    }
}
void prepareHirelingActions(Archives &archives,const ClassicData &data,const AnimDataTable &animations,
    const MonsterRecord &monster,const HirelingDefinition &row,const server::companions::HirelingEquipment &gear,server::companions::PreparedHireling &result) {
    using Action=server::MonsterAttackRule::Action;
    auto &rule=result.rule;const auto &skills=data.tables.at("skills");
    const DataTable sequences(archives.read("data/global/excel/monseq.txt"));
    constexpr std::array modes{"DT","NU","WL","GH","A1","A2","BL","SC","S1","S2","S3","S4","DD","KB","SQ","RN"};
    const auto clock=[&](server::MonsterAttackRule &action,int mode,int skill=0) {
        if(mode<0 || size_t(mode)>=modes.size()) throw std::runtime_error("Invalid PD2 hireling skill mode");
        // Hireling.txt Mode selects the monster animation. Skills.anim is
        // the player clock and must not replace (for example) A3's SC.
        auto motion=std::string_view(modes[size_t(mode)]);
        if(mode==14) {
            const auto &stats=data.tables.at("monstats");
            for(int slot=1;slot<=8;++slot) {
                const auto name=stats.value(monster.sourceRow,"Skill"+std::to_string(slot));
                if(!name.empty() && skills.number(findSkillTableRow(skills,"skill",name),"Id")==skill)
                    motion=stats.value(monster.sourceRow,"Sk"+std::to_string(slot)+"mode");
            }
            if(!motion.starts_with("seq_")) throw std::runtime_error("Missing PD2 hireling MonSeq link");
        }
        int event=mode==4 && monster.attack1Projectile?2:1;
        if(mode==7) {
            const auto weapon=monsterModeWeapon(archives,monster.token,"sc",monster.baseWeapon);
            if(!loadMonsterActionTiming(animations,monster.token,"sc",weapon,1) &&
                loadMonsterActionTiming(animations,monster.token,"sc",weapon,2)) event=2;
        }
        if(!prepareMonsterActionClock(archives,animations,sequences,monster,motion,event,action))
            throw std::runtime_error("Missing PD2 hireling action clock: "+std::string(motion));
        action.nativeMode=uint8_t(mode);
        action.casting=mode==7;action.useAttackRate=mode==4 || mode==5 || mode==14;
    };
    server::MonsterAttackRule ordinary{rule.minimumDamage,rule.maximumDamage,rule.attackRating,rule.attackTicks,rule.impactTick,{}};
    if(row.defaultChance>0) clock(ordinary,4);
    int defaultSkill=0;
    if(row.act==1) {
        const auto name=data.tables.at("monstats").value(monster.sourceRow,"Skill1");
        const auto skill=findSkillTableRow(skills,"skill",name);defaultSkill=skillTableNumber(skills,skill,"Id");
        const auto &program=data.nativeFormulas->skills.at(defaultSkill);
        auto missile=program.reference("srvmissile");if(missile.empty()) missile=program.reference("srvmissilea");
        const auto id=data.nativeFormulas->missileIds.find(std::string(missile));
        if(id==data.nativeFormulas->missileIds.end() || !(ordinary.missile=prepareMonsterMissile(data,id->second,1)))
            throw std::runtime_error("Missing PD2 hireling default missile");
    }
    rule.skillIds[0]=uint16_t(defaultSkill);
    if(row.defaultChance>0) rule.skillActions.emplace(uint16_t(defaultSkill),ordinary);
    result.maintainPartyBuff=row.id==25 || row.id==27 || row.id==29;
    const int delta=result.source.record.level-row.level;
    for(const auto &skill:row.skills) {
        const auto found=result.baseSkillRanks.find(skill.id);
        if(found==result.baseSkillRanks.end() || found->second<=0) continue;
        const auto source=findSkillTableRow(skills,"Id",std::to_string(skill.id));
        const auto start=skills.number(source,"srvstfunc").value_or(0),function=skills.number(source,"srvdofunc").value_or(0);
        // Masteries/Dodge/Evade are intrinsic skills, not chance-zero attacks.
        if(skills.number(source,"passive").value_or(0) || (!start && !function && skills.value(source,"srvmissile").empty())) continue;
        const auto *entry=data.skills.find(skill.id);
        const int rank=gear.skillRanks.at(skill.id);
        server::companions::HirelingAction action{skill.id,rank,
            std::max(0,skill.chance+delta*skill.chancePerLevel/4),skill.mode,skill.aiType};
        if(entry && entry->auraImplemented) action.aura=resolveAura(data,skill.id,rank,result.baseSkillRanks);
        else {
            auto attack=ordinary;attack.missile.reset();attack.rank=uint8_t(found->second);
            attack.animation.reset();attack.animationFlags.clear();attack.animationEvents.clear();attack.releaseFrames.clear();
            clock(attack,skill.mode,skill.id);
            const auto state=skills.value(source,"aurastate");
            if(data.states.contains(state)) action.state=data.states.at(std::string(state)).definition.id;
            if((function==6 || function==30) && !skills.value(source,"auratargetstate").empty()) attack.action=Action::Curse;
            else if(function==18) attack.action=Action::DefensiveBuff;
            else if(start==38 && function==76) attack.action=Action::Whirlwind;
            else if(entry && entry->spell && entry->spell->weapon && entry->spell->weapon->nativeMelee()) {
                SkillCastSpec cast;cast.sourceId=skill.id;cast.rank=found->second;cast.weapon=entry->spell->weapon;
                attack.weaponSkill=std::move(cast);attack.action=Action::Damage;
            } else if((entry && entry->spell) || (!start && (function==0 || function==8 || function==28 || function==160)) ||
                start==4 || start==8) attack.action=Action::Skill;
            else throw std::runtime_error("Unprepared PD2 hireling skill: "+std::to_string(skill.id));
            rule.skillActions.emplace(uint16_t(skill.id),std::move(attack));
        }
        result.actions.push_back(std::move(action));
    }
}
}
