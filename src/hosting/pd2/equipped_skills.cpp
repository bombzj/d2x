#include "equipped_skills.hpp"
#include "content/classic_data.hpp"
#include "content/pd2/skill_data.hpp"
#include "content/skills/table_values.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include <algorithm>

namespace d2x {
void preparePd2EquippedSkills(const ClassicData &data,server::SkillRules &rules) {
    const auto &table=data.tables.at("skills");
    for(size_t row=0;row<table.rows().size();++row) {
        if(!table.value(row,"charclass").empty() || !table.number(row,"aura").value_or(0)) continue;
        const auto id=skillTableNumber(table,row,"Id"),action=table.number(row,"srvdofunc").value_or(0);
        if(rules.definitions.contains(id)) continue;
        SkillRuleSpec spec;
        if(action==18 || action==23) {
            auto definition=preparePd2SkillDefinition(data,row);definition.effect=SkillBehavior::NativeDefensiveBuff;
            definition.state=data.states.at(std::string(table.value(row,"aurastate"))).definition;
            spec=definition.rules();
        } else if(action==29 || action==54) {
            // The equipment-only records use the same native Do program as
            // the class skill. Formula evaluation retains their own record ID.
            const auto prototype=std::find_if(data.skills.skills.begin(),data.skills.skills.end(),[&](const auto &entry) {
                if(!entry.second.spell || entry.second.classCode.empty()) return false;
                const auto source=findSkillTableRow(table,"Id",std::to_string(entry.first));
                return table.number(source,"srvdofunc")==action;
            });
            if(prototype==data.skills.skills.end()) continue;
            auto definition=*prototype->second.spell;definition.sourceId=id;
            definition.state=data.states.at(std::string(table.value(row,"aurastate"))).definition;
            definition.pd2=preparePd2SkillDefinition(data,row).pd2;
            if(action==29) definition.missileId=definition.pd2->missiles[0];
            spec=definition.rules();
        } else continue;
        rules.definitions.emplace(id,server::SkillDefinition{std::move(spec),table.number(row,"InTown").value_or(0)!=0,{}});
    }
}
}
