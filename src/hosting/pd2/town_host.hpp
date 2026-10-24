#pragma once
#include <memory>
namespace d2x {
class Archives;
struct LevelRecord;
struct MonsterRegionRules;
namespace hosting::pd2 {
// Layout admission only; opening an item and running map events are separate
// authoritative programs. All geometry still comes from the current MPQ.
void validateArea(const LevelRecord &);
std::shared_ptr<const MonsterRegionRules> prepareTownRegionRules(Archives &);
}
}
