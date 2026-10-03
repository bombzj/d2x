#include "gameplay/skills/weapon_contributions.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void applyWeaponSkillElements(AttackElements &elements, const WeaponDamage &weapon,
                              const SkillCastSpec *skill, uint64_t &random, unsigned &vengeanceHit) {
    if (skill && skill->effect == SkillBehavior::Vengeance) {
        const int minimum = std::max(256, weapon.meleeBaseMinimum);
        const int maximum = std::max(minimum + 256, weapon.meleeBaseMaximum);
        const int base = minimum + int(limitedRandom(random, unsigned(maximum - minimum)));
        elements.fire += float(int64_t(base) * skill->weapon->elementPercent[0] / 100) / 256.f;
        elements.cold += float(int64_t(base) * skill->weapon->elementPercent[1] / 100) / 256.f;
        elements.lightning += float(int64_t(base) * skill->weapon->elementPercent[2] / 100) / 256.f;
        elements.coldDuration += skill->coldDuration;
    }
    elements.hitClass = weapon.hitClass;
    if (skill && skill->effect == SkillBehavior::Vengeance) {
        elements.hitClass = 32 + int(vengeanceHit % 3) * 16;
        vengeanceHit = (vengeanceHit + 1) % 3;
    }
    if (skill && skill->effect == SkillBehavior::Charge) {
        elements.knockback = true;
        elements.hitClass = 112;
    }
    if (skill && skill->weapon) elements.selfDamagePercent = skill->weapon->selfDamagePercent;
}
} // namespace d2x
