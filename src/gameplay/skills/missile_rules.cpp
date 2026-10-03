#include "gameplay/skills/missile_rules.hpp"
#include "gameplay/skills/missile.hpp"

namespace d2x {
DamageType missileElement(const Missile &missile) {
    return missile.fixedElement.value_or(
        missile.behavior == SkillBehavior::Nova || missile.behavior == SkillBehavior::ChargedBolt ? DamageType::Lightning :
        missile.chill > 0 ? DamageType::Cold : DamageType::Fire);
}
bool missilePierces(const Missile &missile) {
    return !missile.killOnHit || missile.behavior == SkillBehavior::Nova ||
        missile.behavior == SkillBehavior::FrostNova || missile.behavior == SkillBehavior::Inferno;
}
bool missileFollowsPath(const Missile &missile) {
    return missile.behavior == SkillBehavior::ChargedBolt || missile.behavior == SkillBehavior::BlessedHammer;
}
} // namespace d2x
