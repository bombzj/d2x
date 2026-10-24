#pragma once
#include "curse_spec.hpp"
#include <optional>

namespace d2x {
int32_t nativeSlowPercentage(int32_t amount,bool player,bool championOrUnique);
// Pure display formula. Damage and target effects remain server-owned.
CurseSpec evaluateCurse(const CurseSpec &spec, int rank);
int32_t curseResistanceAdjustment(int32_t amount,int32_t baseResistance,bool monster,bool hireling,bool pd2=false);
// sub_6FD10EC0 subtracts a separately truncated percentage. Zero is an
// untimed list, negative resistance extends it, and only Curse states veto.
std::optional<int32_t> curseStateDuration(int32_t frames,int32_t resistance,bool curse,bool pd2=false);
} // namespace d2x
