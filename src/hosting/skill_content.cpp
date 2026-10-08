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
        if (skill.lightningMasteryPerRank) prepared->lightningMasteries.emplace(id, *skill.lightningMasteryPerRank);
        if (skill.coldPiercePerRank) prepared->coldMasteries.emplace(id, *skill.coldPiercePerRank);
        if (!skill.spell) continue;
        const auto &spec = *skill.spell;
        server::SkillDefinition definition{spec.rules(), skill.allowedInTown, {}};
        if (spec.missileId >= 0) {
            const auto collision = data.missileCollisions.find(spec.missileId);
            if (collision != data.missileCollisions.end()) definition.collision = collision->second;
        }
        prepared->definitions.emplace(id, std::move(definition));
    }
    prepared->hydra = data.skills.hydra;
    prepared->collisions = data.missileCollisions;
    prepared->returnFire = data.missileReturnFire;
    prepared->staticMinimum = data.staticFieldMinimum;
    prepared->coldDivisor = data.monsterColdDivisor;
    prepared->freezeDivisor = data.monsterFreezeDivisor;
    const auto &missiles = data.tables.at("missiles");
    for (size_t row = 0; row < missiles.rows().size(); ++row)
        if (const auto id = missiles.number(row, "Id")) prepared->clientSend.emplace(*id, missiles.number(row, "ClientSend").value_or(0) != 0);
    rules.skills = std::move(prepared);
}
}
