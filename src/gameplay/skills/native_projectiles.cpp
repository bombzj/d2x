#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/native_cast.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/skills/projectile_source.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "core/random.hpp"
#include <utility>

namespace d2x {
namespace {
float rollDamage(uint64_t &random, float minimum, float maximum) {
    const int low = int(minimum * 256.f), high = int(maximum * 256.f);
    if (high <= low) return float(low) / 256.f;
    rollRandom(random);
    return float(low + int(uint32_t(random) % unsigned(high - low))) / 256.f;
}
} // namespace
void SkillRuntime::releaseNativeBurst(SkillProjectileSource actor, int missileId, const NativeSkillCast &definition) {
    const auto *skill = &definition.skill;
    auto launch = [&](Vec heading, int index) {
        Missile missile{world_.allocate(), actor.id, actor.pos, heading.unit() * skill->missileVelocity,
            skill->missileLifetime, skill->effect, false, missileId,
            rollDamage(actor.combatRandom, skill->minimumDamage, skill->maximumDamage), 0, skill->coldDuration, true};
        missile.combatRandom = world_.childSeed();
        missile.fixedElement = definition.element;
        missile.killOnHit = definition.killOnHit;
        missile.nextHitDelay = skill->missileNextDelay;
        missile.acceleration = skill->missileAcceleration;
        missile.maxVelocity = skill->missileMaxVelocity;
        if (missileId == 195) {
            const auto path = chargedBoltPath(actor.pos, actor.pos + heading, index,
                                              int(skill->missileLifetime * 25.f));
            missile.path.assign(path.begin(), path.end());
        }
        world_.enqueueMissile(std::move(missile));
    };
    if (missileId == 195) {
        constexpr Vec directions[]{{0,-1},{1,0},{0,1},{-1,0}};
        for (auto heading : directions)
            for (int index = 0; index < 2; ++index) launch(heading, index);
    } else {
        constexpr int offsets[]{30,29,29,28,27,26,24,23,21,19,16,14,11,8,5,2,
            0,-2,-5,-8,-11,-14,-16,-19,-21,-23,-24,-26,-27,-28,-29,-29,
            -30,-29,-29,-28,-27,-26,-24,-23,-21,-19,-16,-14,-11,-8,-5,-2,
            0,2,5,8,11,14,16,19,21,23,24,26,27,28,29,29};
        for (int i = 0; i < 64; ++i) launch({float(offsets[i]), float(offsets[(i + 48) % 64])}, i);
    }
    emit(MissileReleased{missileId});
}
} // namespace d2x
