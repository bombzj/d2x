#include "description_data.hpp"
#include "content/string_table.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <tuple>

namespace d2x {
namespace {
std::string lower(std::string_view value) {
    std::string result(value);
    for (auto &letter:result) letter=char(std::tolower(static_cast<unsigned char>(letter)));
    return result;
}
std::string descriptionText(const ClassicStrings &strings,std::string_view key) {
    if (key.empty()) return {};
    return strings.index(key)<0?"?":std::string(strings.find(key));
}
SkillDescriptionSource source(const DataTable &table, size_t row, bool missile = false) {
    SkillDescriptionSource result;
    static const std::set<std::string,std::less<>> calculations{
        "tohitcalc","dmgsympercalc","edmgsympercalc","elensympercalc","auralencalc","aurarangecalc",
        "calc1","calc2","calc3","calc4","passivecalc1","passivecalc2","passivecalc3","passivecalc4","passivecalc5",
        "aurastatcalc1","aurastatcalc2","aurastatcalc3","aurastatcalc4","aurastatcalc5","aurastatcalc6",
        "petmax","skpoints","dmgcalc"};
    for (const auto &column:table.columns()) {
        const auto key=lower(column);
        if (calculations.contains(key)) {
            // An absent ToHitCalc selects the native linear branch. A real
            // formula returning zero must not silently select another branch.
            if (key!="tohitcalc" || !table.value(row,column).empty())
                result.formulas.emplace(key,compileDescriptionFormula(table.value(row,column)));
        } else {
            static const std::set<std::string,std::less<>> scalars{
                "emin","emax","elen","mindam","maxdam","mindamage","maxdamage","hitshift",
                "mana","lvlmana","manashift","minmana","tohit","levtohit","srcdam",
                "noratio","hcidx","level","level(n)","level(h)","passive","kick"};
            // Skills.Range is a targeting token (h2h/rng/both/none), whereas
            // Missiles.Range and LevRange are integer lifetime fields.
            const bool numeric=(missile && (key=="range" || key=="levrange")) ||
                scalars.contains(key) || key.starts_with("param") || key.starts_with("cltparam") ||
                key.starts_with("hitpar") || key.starts_with("clthitpar") || key.starts_with("dmgparam") ||
                key.starts_with("eminlev") || key.starts_with("emaxlev") || key.starts_with("minelev") ||
                key.starts_with("maxelev") || key.starts_with("elevlen") || key.starts_with("minlevdam") ||
                key.starts_with("maxlevdam") || key.starts_with("minhp") || key.starts_with("maxhp") ||
                key.starts_with("a1mind") || key.starts_with("a1maxd") || key.starts_with("l-hp") || key.starts_with("l-dm");
            if (numeric) {
                if (const auto value=table.number(row,column)) result.numbers.emplace(key,*value);
                else if (table.value(row,column).empty()) result.numbers.emplace(key,0);
            } else if (key=="etype" || key=="summon" || key=="submissile1" || key=="passivestate" ||
                       key.starts_with("passivestat") || key.starts_with("aurastat"))
                result.references.emplace(key,lower(table.value(row,column)));
        }
    }
    return result;
}
SkillDescriptionLine line(const DataTable &table,size_t row,std::string_view group,int slot,const ClassicStrings &strings) {
    const auto field=std::string(group), index=std::to_string(slot);
    SkillDescriptionLine result;
    const auto programField=field+"line"+index;
    const auto program=table.number(row,programField);
    result.program=program.value_or(table.value(row,programField).empty()?0:-1);
    if (!result.program) return result;
    result.textA=descriptionText(strings,table.value(row,field+"texta"+index));
    result.textB=descriptionText(strings,table.value(row,field+"textb"+index));
    result.calculationA=compileDescriptionFormula(table.value(row,field+"calca"+index));
    result.calculationB=compileDescriptionFormula(table.value(row,field+"calcb"+index));
    return result;
}
SkillDescriptionDefinition description(const DataTable &table,size_t row,const ClassicStrings &strings) {
    SkillDescriptionDefinition result;
    result.damageProgram=table.number(row,"descdam").value_or(0);
    result.attackProgram=table.number(row,"descatt").value_or(0);
    for(size_t slot=0;slot<2;++slot) result.damageCalculations[slot]=compileDescriptionFormula(table.value(row,"ddam calc"+std::to_string(slot+1)));
    for (size_t slot=0;slot<result.levels.size();++slot) result.levels[slot]=line(table,row,"desc",int(slot+1),strings);
    for (size_t slot=0;slot<result.general.size();++slot) result.general[slot]=line(table,row,"dsc2",int(slot+1),strings);
    for (size_t slot=0;slot<result.bonuses.size();++slot) result.bonuses[slot]=line(table,row,"dsc3",int(slot+1),strings);
    result.manaLabel=descriptionText(strings,table.value(row,"str mana"));
    for (size_t slot=0;slot<3;++slot) {
        const auto index=std::to_string(slot+1);
        result.missiles[slot]=lower(table.value(row,"descmissile"+index));
    }
    return result;
}
}
std::optional<int> SkillDescriptionSource::number(std::string_view field) const {
    const auto found=numbers.find(field);
    return found==numbers.end()?std::nullopt:std::optional{found->second};
}
std::string_view SkillDescriptionSource::reference(std::string_view field) const {
    const auto found=references.find(field);
    return found==references.end()?std::string_view{}:found->second;
}
std::string_view SkillDescriptionCatalog::label(std::string_view key) const {
    if (key.empty()) return {};
    const auto found=labels.find(key);
    return found==labels.end()?std::string_view{"?"}:found->second;
}
SkillDescriptionCatalog loadSkillDescriptions(const std::map<std::string,DataTable,std::less<>> &tables,
                                              const ClassicStrings &strings) {
    SkillDescriptionCatalog result;
    const auto &descriptions=tables.at("skilldesc"), &skills=tables.at("skills");
    std::map<std::string,SkillDescriptionDefinition,std::less<>> definitions;
    for (size_t row=0;row<descriptions.rows().size();++row) {
        const auto key=lower(descriptions.value(row,"skilldesc"));
        if (!key.empty() && !definitions.emplace(key,description(descriptions,row,strings)).second)
            throw std::runtime_error("Duplicate SkillDesc description");
    }
    for (size_t row=0;row<skills.rows().size();++row) {
        const auto name=lower(skills.value(row,"skill")); const auto id=skills.number(row,"Id");
        if (name.empty() || !id || *id<0) continue;
        auto record=source(skills,row); record.id=*id;
        if (record.number("passive")==1) {
            std::set<std::string,std::less<>> statNames;
            for (unsigned slot=1;slot<=5;++slot) {
                const auto stat=record.reference("passivestat"+std::to_string(slot));
                if (!stat.empty() && statNames.emplace(stat).second)
                    result.passives[std::string(stat)].push_back({*id,std::string(record.reference("passivestate"))});
            }
        }
        if (const auto definition=definitions.find(lower(skills.value(row,"skilldesc")));definition!=definitions.end()) {
            record.description=definition->second; record.hasDescription=true;
        }
        if (!result.skills.emplace(*id,std::move(record)).second || !result.skillIds.emplace(name,*id).second)
            throw std::runtime_error("Duplicate skill description formula identity");
    }
    std::set<std::string,std::less<>> summonedMonsters;
    for (const auto &[id,record]:result.skills)
        if (!record.reference("summon").empty()) summonedMonsters.emplace(record.reference("summon"));
    for (const auto &[tableName,identity,destination]:{
        std::tuple{"missiles","Missile",&result.missiles},std::tuple{"monstats","Id",&result.monsters}}) {
        const auto &table=tables.at(tableName);
        for (size_t row=0;row<table.rows().size();++row) {
            const auto key=lower(table.value(row,identity)); if (key.empty() || key=="expansion") continue;
            // MonStats has repeated textual IDs with distinct hcIdx records.
            // Only summon references and program 41's native base identity are
            // description inputs; unrelated duplicate NPC names are not keys.
            if (std::string_view(tableName)=="monstats" && !summonedMonsters.contains(key) &&
                table.number(row,"hcIdx")!=292) continue;
            auto record=source(table,row,std::string_view(tableName)=="missiles"); record.id=int(row);
            if (!destination->emplace(key,std::move(record)).second) throw std::runtime_error("Duplicate skill description source");
        }
    }
    const auto &levels=tables.at("monlvl");
    for (size_t row=0;row<levels.rows().size();++row) result.monsterLevels.push_back(source(levels,row));
    const auto &difficulty=tables.at("difficultylevels");
    for (size_t index=0;index<3;++index) result.aiCurseDivisors[index]=difficulty.number(index,"AiCurseDiv");
    if (const auto stats=tables.find("itemstatcost");stats!=tables.end())
        for (size_t row=0;row<stats->second.rows().size();++row) {
            const auto name=lower(stats->second.value(row,"Stat"));
            if (name.empty() || !stats->second.has("ValShift")) continue;
            if (const auto shift=stats->second.number(row,"ValShift")) result.statShifts.emplace(name,*shift);
            else if (stats->second.value(row,"ValShift").empty()) result.statShifts.emplace(name,0);
        }
    // Original program literals are string keys, never translated English labels.
    for (int index=1;index<=125;++index) {
        const auto key="StrSkill"+std::to_string(index); const auto text=strings.find(key);
        if (strings.index(key)>=0) result.labels.emplace(key,text);
    }
    for (const auto *key:{"space","colon","percent","dash","StrSkill63Patch","skilldesc3","StrSkill43a","StrSkill44a","StrSkill18a",
        "StrSkill18b","scrollbooktext","WeapDamsk","strSkillPoisonLength","ModStr2z","ItemStats1o","ModStre10d"})
        if (strings.index(key)>=0) result.labels.emplace(key,strings.find(key));
    // Formatted program strings may be expansion keys rather than StrSkillN.
    for (const auto &[key,text]:strings.entries())
        if (key.starts_with("Eskill") || key.starts_with("eskill") || key.starts_with("strSkill") || key.starts_with("strEskill"))
            result.labels.emplace(key,text);
    return result;
}
} // namespace d2x
