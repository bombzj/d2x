#include "skill_sources.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <algorithm>

namespace d2x {
ItemSkillLayer decodeItemSkillLayer(int layer) {
    return {layer >> 6, layer & 63};
}
ChargedSkill decodeChargedSkill(ItemHandle item, int layer, int value) {
    const auto source = decodeItemSkillLayer(layer);
    return {item, source.skill, source.rank, value & 255, (value >> 8) & 255, layer};
}
std::vector<ChargedSkill> statListChargedSkills(const StatList &root,const StatRuleCatalog &rules,
    const std::function<std::optional<ItemHandle>(EntityId)> &handle) {
    std::vector<ChargedSkill> result;
    const auto charged=rules.names.at("item_charged_skill");
    const auto visit=[&](auto &&self,const StatList &list)->void {
        if(list.ownerKind==StatOwnerKind::Item) if(const auto item=handle(list.source.owner)) {
            for(const auto &[key,value]:list.base) if(key.id==charged) {
                const auto charge=decodeChargedSkill(*item,key.layer,value);
                if(charge.rank<=0) continue;
                std::erase_if(result,[&](const auto &entry){return entry.item.id==charge.item.id && entry.skill==charge.skill;});
                if(charge.charges>0) result.push_back(charge);
            }
        }
        for(const auto &child:list.contributions) if(child) self(self,*child);
    };
    visit(visit,root);return result;
}
std::optional<ItemSkillEvent> itemSkillEvent(std::string_view stat) {
    if (stat == "item_skillonattack") return ItemSkillEvent::Attack;
    if (stat == "item_skillonhit") return ItemSkillEvent::Hit;
    if (stat == "item_splashonhit") return ItemSkillEvent::MeleeSplash;
    if (stat == "item_skillonkill") return ItemSkillEvent::Kill;
    if (stat == "item_skillongethit") return ItemSkillEvent::GetHit;
    if (stat == "item_skillondeath") return ItemSkillEvent::Death;
    if (stat == "item_skillonlevelup") return ItemSkillEvent::LevelUp;
    return {};
}
}
