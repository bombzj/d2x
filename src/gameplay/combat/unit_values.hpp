#pragma once
#include <cstdint>
namespace d2x {
int32_t unitDefense(int32_t dexterity,int32_t armor,int32_t itemPercent,int32_t skillPercent,int32_t holyShieldPercent=0,int32_t overridePercent=0);
int32_t unitAttackRating(int32_t dexterity,int32_t toHit,int32_t classFactor);
int32_t unitBlockChance(int32_t block,int32_t dexterity,int32_t level);
}
