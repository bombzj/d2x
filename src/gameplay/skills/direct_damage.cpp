#include "gameplay/combat/damage_request.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SkillRuntime::releaseStaticField(SkillCaster player, const SkillCastSpec &skill, int staticFieldMinimum) {
    for (auto unit : combatUnits()) {
        if (!unit.alive() || !canAttack(player.id, unit.id) || !active(*unit.position) ||
            (*unit.position - player.pos).length() > skill.staticRadius) continue;
        const int hitpoints = int(*unit.life);
        if (hitpoints < 1 || (staticFieldMinimum > 0 && hitpoints <= unit.stats.attributes.maxLife * staticFieldMinimum / 100)) continue;
        float amount = std::max(skill.staticMinDamage, float(std::min(hitpoints * int(skill.staticPercent) / 100, hitpoints - 1)));
        amount *= float(std::clamp(100 - rawResistance(unit.stats.attributes, MonsterDamageType::Lightning), 0, 100)) / 100.f;
        dealDamage({player.id, unit.id, amount, MonsterDamageType::Lightning, 0, true});
    }
}
} // namespace d2x
