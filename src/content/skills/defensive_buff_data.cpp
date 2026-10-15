#include "defensive_buff_data.hpp"
#include "table_values.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include <stdexcept>

namespace d2x {
void loadNativeDefensiveBuffs(SkillCatalog &catalog,const DataTable &skills,const DataTable &overlays,
    const DataTable &sounds,const CombatStateCatalog &states,Archives &archives) {
    const auto overlay=[&](std::string_view name) {
        SkillOverlayVisual visual;if(name.empty()) return visual;
        const auto row=findSkillTableRow(overlays,"overlay",name);
        const auto number=[&](std::string_view field) {return skillTableNumber(overlays,row,field);};
        visual.id=int(row);visual.frames=number("Frames");visual.fps=float(number("AnimRate"));visual.trans=number("Trans");
        visual.preDraw=overlays.number(row,"PreDraw").value_or(0)!=0;
        visual.offset={-float(number("Xoffset")),float(number("Yoffset"))};
        for(int height=0;height<4;++height) visual.heights[height]=number("Height"+std::to_string(height+1));
        visual.art="data/global/overlays/"+std::string(overlays.value(row,"Filename"))+".dcc";
        if(visual.frames<=0 || visual.fps<=0 || !archives.contains(visual.art)) throw std::runtime_error("Missing original defensive buff overlay: "+visual.art);
        return visual;
    };
    const auto sound=[&](std::string_view name) {
        if(name.empty()) return std::string{};
        const auto row=findSkillTableRow(sounds,"Sound",name);
        auto art="data/global/sfx/"+std::string(sounds.value(row,"FileName"));
        if(!archives.contains(art)) throw std::runtime_error("Missing original defensive buff sound: "+art);
        return art;
    };
    for(size_t row=0;row<skills.rows().size();++row) {
        if(skills.number(row,"srvdofunc")!=18) continue;
        const auto character=skills.value(row,"charclass");if(character!="dru" && character!="ass") continue;
        const auto id=skills.number(row,"Id");if(!id) continue;
        const auto found=catalog.skills.find(*id);if(found==catalog.skills.end() || found->second.spell) continue;
        requireSkillTableText(skills,row,"anim","SC");
        const auto state=states.find(skills.value(row,"aurastate"));
        if(state==states.end()) throw std::runtime_error("Missing original defensive buff state");
        SkillSpec spec;spec.effect=SkillBehavior::NativeDefensiveBuff;spec.sourceId=*id;spec.state=state->second.definition;
        spec.mana=skillTableNumber(skills,row,"mana");spec.minimumMana=skillTableNumber(skills,row,"minmana");
        spec.manaPerLevel=skillTableNumber(skills,row,"lvlmana");spec.manaShift=skillTableNumber(skills,row,"manashift");
        spec.hitShift=skillTableNumber(skills,row,"HitShift",SkillNumberPolicy::OptionalBlankIsZero);
        spec.delayFrames=skillTableNumber(skills,row,"delay",SkillNumberPolicy::OptionalBlankIsZero);
        spec.castSoundArt=sound(skills.value(row,"stsound"));spec.activationSoundArt=sound(skills.value(row,"dosound"));
        spec.castOverlay=overlay(skills.value(row,"castoverlay"));spec.stateOverlay=overlay(state->second.overlay);
        found->second.spell=std::make_shared<const SkillSpec>(std::move(spec));
    }
}
}
