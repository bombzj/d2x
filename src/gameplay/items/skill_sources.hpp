#pragma once
#include "gameplay/items/handle.hpp"
#include <vector>

namespace d2x {
struct ItemSkillGrant {
    ItemHandle item;
    int skill = -1, rank = 0;
};
// Native ItemStatCost layer: skill ID in the high bits, level in six low bits.
enum class ItemSkillEvent { Attack, Hit, Kill, GetHit, Death, LevelUp };
struct SkillCharge { ItemHandle item; int layer{}; };
struct ChargedSkill { ItemHandle item; int skill{}, rank{}, charges{}, maximum{}, layer{}; };
} // namespace d2x
