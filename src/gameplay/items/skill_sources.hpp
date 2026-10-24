#pragma once
#include "gameplay/items/handle.hpp"
#include <optional>
#include <string_view>
#include <vector>
#include <functional>

namespace d2x {
struct StatList;struct StatRuleCatalog;
struct ItemSkillGrant {
    ItemHandle item;
    int skill = -1, rank = 0;
};
// Native ItemStatCost layer: skill ID in the high bits, level in six low bits.
enum class ItemSkillEvent { Attack, Hit, Kill, GetHit, Death, LevelUp, MeleeSplash };
struct SkillCharge { ItemHandle item; int layer{}; };
struct ChargedSkill { ItemHandle item; int skill{}, rank{}, charges{}, maximum{}, layer{}; };
struct ItemSkillLayer { int skill{}, rank{}; };
ItemSkillLayer decodeItemSkillLayer(int layer);
ChargedSkill decodeChargedSkill(ItemHandle item, int layer, int value);
std::vector<ChargedSkill> statListChargedSkills(const StatList &,const StatRuleCatalog &,
    const std::function<std::optional<ItemHandle>(EntityId)> &);
std::optional<ItemSkillEvent> itemSkillEvent(std::string_view stat);
} // namespace d2x
