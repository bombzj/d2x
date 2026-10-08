#include "skill_content.hpp"
#include "content/classic_data.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "gameplay/skills/behavior.hpp"
namespace d2x {
void prepareSkillRules(server::PreparedRules &rules, const ClassicData &data, const CharacterDefinition &character) {
    auto prepared = std::make_shared<server::SkillRules>();
    const auto prefix = character.appearance + "sc";
    for (const auto &[key, animation] : data.skills.castTimings)
        if (key.starts_with(prefix)) prepared->animations.emplace(key.substr(prefix.size()),
            CastAnimationTiming{animation.frames, animation.speed, animation.actionFrame});
    for (const auto &[id, skill] : data.skills.skills) {
        if (skill.classCode != character.code || character.code != "sor") continue;
        if (skill.fireMasteryPerRank) prepared->fireMasteries.emplace(id, *skill.fireMasteryPerRank);
        if (!skill.spell) continue;
        const auto &spec = *skill.spell;
        if (spec.effect != SkillBehavior::FireBolt && spec.effect != SkillBehavior::Fireball && spec.effect != SkillBehavior::Teleport) continue;
        server::SkillDefinition definition{spec, skill.allowedInTown, {}};
        if (spec.effect != SkillBehavior::Teleport) {
            const auto collision = data.missileCollisions.find(spec.missileId);
            if (collision == data.missileCollisions.end()) continue;
            // These examples use the old straight/area-impact executor. Other
            // child programs require their own domain operation, not a fallback.
            if (spec.missileImpact && (spec.missileImpact->cloudBurst || spec.missileImpact->areaMissile)) continue;
            definition.collision = collision->second;
        }
        prepared->definitions.emplace(id, std::move(definition));
    }
    rules.skills = std::move(prepared);
}
}
