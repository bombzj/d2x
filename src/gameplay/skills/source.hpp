#pragma once
#include "core/id.hpp"
#include <functional>
#include <map>

namespace d2x {
struct SkillSpec;
struct SkillCastSpec;

// Returned by an authority-side adapter at the original evaluation time.
// The map is borrowed only during resolve(); it is never stored in a cast.
struct SkillSourceValues {
    const std::map<int, int> &synergyRanks;
    int fireMasteryPercent = 0, lightningMasteryPercent = 0, coldDamagePercent = 0;
};

// Owns bindings, not actor state. Actor lifecycle owners must unbind a source
// before destroying it. Unknown IDs never fall back to a local player.
class UnitSkillSources {
    std::map<EntityId, std::function<SkillSourceValues()>> sources_;

  public:
    void bind(EntityId actor, std::function<SkillSourceValues()> source);
    void unbind(EntityId actor);
    SkillCastSpec resolve(const SkillSpec &spec, EntityId actor, int rank) const;
};
} // namespace d2x
