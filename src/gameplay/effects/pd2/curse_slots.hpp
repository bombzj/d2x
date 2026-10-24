#pragma once
#include "gameplay/effects/definition.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <span>
namespace d2x {
struct ActiveCombatEffect;
struct CombatEffectSpec;
struct Pd2CurseSlots {int count{};StatKey stat;int effectiveness{};bool preserveExisting{};};
struct Pd2CursePosting {uint64_t replacement{};bool changed{};};
Pd2CursePosting preparePd2CurseSlots(std::span<ActiveCombatEffect>,CombatEffectSpec &,EffectFrame);
int32_t pd2CursePrimaryValue(int32_t value,int32_t effectiveness);
}
