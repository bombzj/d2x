#pragma once
#include "core/math.hpp"
#include <cstdint>
#include <vector>
#include <utility>
namespace d2x {
// MonsterMode extra quills: caller supplies its own SEIS stream, both sides
// keep the original persistent sign flips and five-cell offsets.
std::vector<Vec> monsterQuillTargets(Vec target, int count, uint64_t &random);
int monsterFacing8(Vec from,Vec to);
Vec monsterDirectionOffset(int index);
std::pair<Vec,Vec> monsterWebTrailPoints(Vec position,Vec movement);
// SkillMonst::SrvDo088 and retail CltDo048 share these direction tables.
std::pair<Vec,Vec> andarielSprayRay(Vec position,Vec capturedTarget,int sequenceFrame);
// SrvDo093 retains the dominant axis; retail CltDo051 uses a cardinal ray.
enum class GargoyleRaySide { Client, Server };
std::pair<Vec,Vec> gargoyleTrapRay(Vec position,Vec target,GargoyleRaySide);
struct MonsterEnchantmentRay { Vec offset; int pathIndex{-1}; };
// Retail charged-bolt initialization: four cardinal rays, two paths each.
std::vector<MonsterEnchantmentRay> monsterLightningRays();
}
