// Rules adapted from D2MOO SkillAma::SrvDo006 / AuraCallback (MIT).
#include "amazon_magic_spec.hpp"
#include "cast_spec.hpp"
#include "world_port.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include <algorithm>

namespace d2x {
void releaseAmazonMagic(ISkillWorld &world, EntityId actor, const SkillCastSpec &skill) {
    const auto &program = *skill.amazonMagic;
    const auto origin = world.position(actor);
    for (auto unit : world.units()) {
        if (!unit.alive() || !unit.effects || !world.active(*unit.position) ||
            !(program.filter & (unit.player ? 1 : 2)) || !world.canAttack(actor, unit.id) ||
            !world.auraEligible(unit.id, false)) continue;
        if ((program.filter & 0x4000) && unit.stats.boss) continue;
        const int x = int(unit.position->x) - int(origin.x), y = int(unit.position->y) - int(origin.y);
        if (x * x + y * y > program.radius * program.radius) continue;
        if ((program.filter & 0x200) && !world.collisionSegment(origin, *unit.position, 0x04)) continue;
        const int resistance = unit.stats.attributes.combat.curseResistance;
        if (resistance >= 100) continue;
        CombatEffectSpec effect;
        effect.state = program.state;
        effect.source = {CombatEffectSource::Skill, actor, skill.sourceId, skill.rank};
        effect.stacking = EffectStacking::CurseLevel;
        effect.duration = EffectFrame(std::max(1, program.frames * (100 - resistance) / 100));
        effect.modifiers.defense = -program.defenseReduction;
        effect.modifiers.combat.slowMissiles = program.slowPercent;
        effect.visual.overlayId = program.overlay;
        const auto applied = unit.effects->apply(std::move(effect), world.frame());
        if (unit.player && applied.accepted) world.effectsChanged(applied.removed);
    }
}
} // namespace d2x
