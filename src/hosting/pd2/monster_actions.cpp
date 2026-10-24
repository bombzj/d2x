#include "monster_actions.hpp"
#include "hosting/monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/pd2/skill_data.hpp"
#include "content/skills/table_values.hpp"
#include "gameplay/monsters/pd2/skill_program.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "server/runtime/skill_rules.hpp"
#include <algorithm>
#include <cctype>
#include <set>
namespace d2x::hosting::pd2 {
namespace {
std::string nativeName(std::string_view value) {
    std::string result(value);
    for(auto &letter:result) letter=char(std::tolower(static_cast<unsigned char>(letter)));
    return result;
}
bool projectile(const ClassicData &data,size_t row) {
    const auto &skills=data.tables.at("skills");
    if(skills.number(row,"passive").value_or(0) ||
        !pd2MonsterProjectileProgram(skills.number(row,"srvstfunc").value_or(0),skills.number(row,"srvdofunc").value_or(0))) return false;
    // An empty Do0 has no skill effect (e.g. the skeleton's raise marker).
    if(!skills.number(row,"srvdofunc").value_or(0) && skills.value(row,"srvmissile").empty()) return false;
    for(const auto field:{"srvmissile","srvmissilea","srvmissileb","srvmissilec"}) {
        const auto name=skills.value(row,field);
        if(!name.empty() && !data.nativeFormulas->missileIds.contains(nativeName(name))) return false;
    }
    return true;
}
}
void prepareMonsterSkillPrograms(const ClassicData &data,server::SkillRules &rules) {
    const auto &monsters=data.tables.at("monstats"),&skills=data.tables.at("skills");
    std::set<std::string> linked;
    for(size_t row=0;row<monsters.rows().size();++row) for(int slot=1;slot<=8;++slot) {
        const auto name=monsters.value(row,"Skill"+std::to_string(slot));
        if(!name.empty()) linked.insert(nativeName(name));
    }
    for(size_t row=0;row<skills.rows().size();++row) {
        const auto id=skills.number(row,"Id");
        if(!id || !linked.contains(nativeName(skills.value(row,"skill"))) || !projectile(data,row)) continue;
        auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Pd2;
        if(const auto found=rules.definitions.find(*id);found!=rules.definitions.end()) {
            if(!found->second.spec.pd2) found->second.spec.pd2=spec.pd2;
        } else rules.definitions.emplace(*id,server::SkillDefinition{spec.rules(),skills.number(row,"InTown").value_or(0)!=0,{}});
    }
}
bool prepareMonsterActions(Archives &archives,const ClassicData &data,const AnimDataTable &animations,
    const MonsterRecord &record,server::MonsterRule &rule) {
    const auto &stats=data.tables.at("monstats"),&skills=data.tables.at("skills");
    // Fn047's nonzero gate owns counters and alternating auras. Its ordinary
    // FlyingScimitar delegate must not impersonate that unresolved program.
    if(record.ai=="FlyingScimitar" && stats.number(record.sourceRow,"aip7(H)").value_or(0)) return false;
    const DataTable sequences(archives.read("data/global/excel/monseq.txt"));
    const int bonus=data.tables.at("difficultylevels").number(size_t(rule.difficulty),"MonsterSkillBonus").value_or(0);
    for(size_t slot=0;slot<rule.skillIds.size();++slot) {
        const auto field=std::to_string(slot+1),name=nativeName(stats.value(record.sourceRow,"Skill"+field));
        if(name.empty()) continue;
        size_t row=0;while(row<skills.rows().size() && nativeName(skills.value(row,"skill"))!=name) ++row;
        if(row==skills.rows().size() || skills.number(row,"passive").value_or(0)) continue;
        // These native programs own additional preparation: the tower selects
        // MissA1 and a sequence event, while Healing Vortex targets allies.
        if((record.ai=="ArcaneTower" && name=="lightning") || name=="healing vortex") continue;
        const int start=skills.number(row,"srvstfunc").value_or(0),function=skills.number(row,"srvdofunc").value_or(0);
        const auto state=skills.value(row,"auratargetstate");
        const bool curse=!start && function==30 && state!="dark_pact" && data.states.contains(state);
        const bool inferno=start==53 && function==95;
        if(!curse && !inferno && !projectile(data,row)) continue;
        const int baseRank=stats.number(record.sourceRow,"Sk"+field+"lvl").value_or(0);
        if(baseRank<=0) continue;
        const int rank=baseRank+bonus;
        const int id=skillTableNumber(skills,row,"Id");
        if(rank<1 || rank>255 || id<1 || id>UINT16_MAX) return false;
        server::MonsterAttackRule action;action.action=server::MonsterAttackRule::Action::Pd2Projectile;action.rank=uint8_t(rank);
        if(curse) {
            action.action=server::MonsterAttackRule::Action::Curse;
            if(state=="decrepify") rule.closeCurseState=data.states.at(std::string(state)).definition.id;
        } else if(inferno) {
            action.action=server::MonsterAttackRule::Action::Inferno;
            action.infernoFrame=record.infernoAnimation;action.infernoEndFrames=record.infernoLength;
            if(action.infernoFrame<0 || action.infernoEndFrames<=0) return false;
            const auto name=nativeName(skills.value(row,"srvmissilea"));
            const auto missile=data.nativeFormulas->missileIds.find(name);
            if(missile==data.nativeFormulas->missileIds.end() || !prepareMonsterMissile(data,missile->second,rank)) return false;
        } else if(!function) {
            // The native default missile callback also copies the monster's
            // attack bonuses; retain that existing admission path.
            const auto definition=data.nativeFormulas->missileIds.at(nativeName(skills.value(row,"srvmissile")));
            action.missile=prepareMonsterMissile(data,definition,rank);
            if(!action.missile) return false;
            action.action=server::MonsterAttackRule::Action::NativeMissile;
        }
        auto mode=stats.value(record.sourceRow,"Sk"+field+"mode");
        int event=record.ai=="Izual" || record.ai=="UberIzual" || record.ai=="BaalCrab" || record.ai=="BaalCrabClone"?1:2;
        if(!hasAct2MonsterSkills(record.ai) && mode=="A1" && !record.attack1Projectile) event=1;
        if(inferno) {
            event=name=="horror arctic blast"?1:2;
            // Fn135 casts the stream in S1, not its declared sequence.
            if(name=="baal inferno") mode="S1";
        }
        if(!prepareMonsterActionClock(archives,animations,sequences,record,mode,event,action)) return false;
        if(inferno && action.infernoFrame>=action.animation->frames) return false;
        const auto sourceMode=stats.value(record.sourceRow,"Sk"+field+"mode");
        action.useAttackRate=sourceMode=="A1" || sourceMode=="A2" || skills.number(row,"UseAttackRate").value_or(0)!=0;
        rule.skillIds[slot]=uint16_t(id);rule.skillActions.insert_or_assign(uint16_t(id),std::move(action));
    }
    return true;
}
}
