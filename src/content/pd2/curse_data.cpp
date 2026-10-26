#include "curse_data.hpp"
#include "skill_data.hpp"
#include "content/classic_data.hpp"
#include "content/skills/table_values.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/behavior.hpp"

namespace d2x {
void preparePd2Curses(ClassicData &data) {
    const auto &table=data.tables.at("skills");
    for(auto &[id,record]:data.skills.skills) {
        if(record.passive) continue;
        const auto row=findSkillTableRow(table,"Id",std::to_string(id));
        const auto action=table.number(row,"srvdofunc").value_or(0);
        if(!table.number(row,"srvstfunc").value_or(0) && action==71) {
            // Current Do71 (0x3ADBF0) runs its Taunt callback over the
            // caster's aura range; retail selects one target within 20.
            auto spell=preparePd2SkillDefinition(data,row);spell.effect=SkillBehavior::Curse;
            CurseSpec curse;curse.state=data.states.at(std::string(table.value(row,"auratargetstate"))).definition;
            curse.targetFilter=table.number(row,"aurafilter").value_or(0);
            curse.ai=CurseAi::Taunt;curse.ignoreDifficultyDivisor=true;
            spell.curse=std::move(curse);record.spell=std::make_shared<const SkillSpec>(std::move(spell));continue;
        }
        const bool itemEffect=record.classCode.empty() && table.number(row,"ItemEffect").value_or(0)>0;
        if(record.classCode!="nec" && !itemEffect) continue;
        if(action!=30 && action!=59 && action!=61) continue;
        const auto state=table.value(row,"auratargetstate");
        const auto number=[&](std::string_view field) {return skillTableNumber(table,row,field,SkillNumberPolicy::BlankIsZero);};
        auto spell=preparePd2SkillDefinition(data,row);spell.effect=SkillBehavior::Curse;
        if(state=="dark_pact") spell.effect=SkillBehavior::Pd2;
        CurseSpec curse;curse.state=data.states.at(std::string(state)).definition;curse.targetFilter=number("aurafilter");
        curse.ai=action==59?CurseAi::Attract:action==61?CurseAi::Confuse:
            state=="dimvision"?CurseAi::DimVision:state=="terror"?CurseAi::Terror:CurseAi::None;
        if(number("auraeventfunc1")==4) {
            curse.reflectPercent=number("Param5");curse.reflectPerLevel=number("Param6");
            curse.reflectReducedDivisor=4;curse.reflectHitClass=number("HitClass");
        }
        if(number("auraeventfunc1")==5) {
            curse.lifeTapPercent=number("Param5");curse.lifeTapPerLevel=number("Param6");
            const auto &overlays=data.tables.at("overlay");const auto overlay=table.value(row,"prgoverlay");
            if(!overlay.empty()) {
                const auto source=findSkillTableRow(overlays,"overlay",overlay);
                curse.healOverlay=int(source);
                const auto rate=overlays.number(source,"AnimRate").value_or(0);
                if(rate>0) curse.healOverlayDuration=float(overlays.number(source,"Frames").value_or(0))/float(rate);
            }
        }
        spell.curse=std::move(curse);record.spell=std::make_shared<const SkillSpec>(std::move(spell));
    }
}
}
