#pragma once
#include <memory>
namespace d2x {
class Archives;
struct LevelRecord;
struct MonsterRegionRules;
namespace hosting::pd2 {
// The first self-hosted slice admits real town definitions only. Unknown
// PD2 combat/map programs must not silently execute the original rules.
void validateTownArea(const LevelRecord &);
std::shared_ptr<const MonsterRegionRules> prepareTownRegionRules(Archives &);
}
}
