#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/missile_rules.hpp"
#include "gameplay/skills/missile_launch_spec.hpp"
#include "gameplay/skills/missile.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void prepareMissileLaunch(Missile &missile, const CombatModifiers &owner, bool canSlow, bool canPierce) {
    if (missile.launch) return;
    auto spec = std::make_shared<MissileLaunchSpec>();
    if (canSlow && owner.slowMissiles > 0) spec->velocityPercent = owner.slowMissiles;
    if (canPierce && owner.pierce > 0) {
        spec->pierceChance = std::clamp(owner.pierce, 0, 100);
        missile.piercing = std::make_shared<MissilePierceState>();
        // Independent launch draw, as MISSILES_CreateMissileFromParams: stop on
        // the first failed check, at most four continuations / five contacts.
        auto random = initialRandom(uint32_t(missile.id.value));
        while (missile.piercing->remaining < 4 && limitedRandom(random, 100) < unsigned(spec->pierceChance))
            ++missile.piercing->remaining;
    }
    missile.velocity = missile.velocity * (float(spec->velocityPercent) / 100.f);
    missile.launch = std::move(spec);
}
bool missileAlreadyHit(const Missile &missile, EntityId target) {
    return missile.piercing && std::find(missile.piercing->hits.begin(), missile.piercing->hits.end(), target) != missile.piercing->hits.end();
}
bool consumeMissilePierce(Missile &missile, EntityId target) {
    if (!missile.piercing) return false;
    if (!missile.piercing.unique()) missile.piercing = std::make_shared<MissilePierceState>(*missile.piercing);
    auto &state = *missile.piercing;
    state.hits.push_back(target);
    if (state.remaining <= 0) return false;
    --state.remaining; return true;
}
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
