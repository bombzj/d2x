#pragma once
#include "server/runtime/contracts.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include <memory>
#include <optional>
namespace d2x {struct SkillCastSpec;}
namespace d2x::server {
class PlayerStore;class AreaStore;struct PlayerState;struct SkillRules;
namespace monsters {class System;struct Actor;}
namespace effects {class System;}
namespace skills {
// A borrowed capability view. The actor stores identity, resources, equipment
// and seed; its controller supplies class program metadata, never its totals.
struct NativeCaster {
    const PlayerState *player{},*controller{};
    const monsters::Actor *monster{};
    EntityId id() const;
    RegionId area() const;
    Vec position() const;
    int level() const;
    uint64_t random() const;
    const SkillRules *programs() const;
    ActorContext context(const AreaStore &,uint64_t tick) const;
    SkillFormulaInput formulaInput(const effects::System &,uint64_t tick,RuleRandom={}) const;
};
std::optional<NativeCaster> nativeCaster(const PlayerStore &,const monsters::System &,EntityId);
SkillCastSpec evaluateNativeSkill(const NativeCaster &,const effects::System &,int skill,int rank,uint64_t tick);
}
}
