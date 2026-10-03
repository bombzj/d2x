#include "gameplay/skills/source.hpp"
#include "gameplay/skills/resolve.hpp"
#include <stdexcept>
#include <utility>

namespace d2x {
void UnitSkillSources::bind(EntityId actor, std::function<SkillSourceValues()> source) {
    if (!actor || !source) throw std::invalid_argument("Invalid skill source binding");
    if (!sources_.emplace(actor, std::move(source)).second)
        throw std::logic_error("Skill source already bound for this actor");
}

void UnitSkillSources::unbind(EntityId actor) {
    sources_.erase(actor);
}

SkillCastSpec UnitSkillSources::resolve(const SkillSpec &spec, EntityId actor, int rank) const {
    const auto found = sources_.find(actor);
    if (found == sources_.end()) throw std::runtime_error("Skill source unavailable for this actor");
    const auto values = found->second();
    return resolveSkill(spec, {rank, values.synergyRanks, values.fireMasteryPercent,
                              values.lightningMasteryPercent, values.coldDamagePercent});
}
} // namespace d2x
