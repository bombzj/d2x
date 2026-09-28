#include "scene_assets.hpp"
#include "content/monster_animation.hpp"
#include <stdexcept>

namespace d2x {
void SceneAssets::loadHirelingAnimations(Archives &archives, const GameSession &session) {
    const HirelingDefinition *hireling = nullptr;
    for (const auto &entry : session.content().hirelings)
        if (entry.act == 1 && entry.difficulty == 1) {
            hireling = &entry;
            break;
        }
    if (!hireling) return;
    const MonsterRecord *actor = nullptr;
    for (const auto &[id, record] : session.monsterContent().monsters())
        if (record.index == hireling->classId) {
            actor = &record;
            break;
        }
    if (!actor)
        throw std::runtime_error("Original Act I hireling actor is missing");
    std::array<const char *, 16> equipment;
    equipment.fill("");
    if (!actor->rightHandVariant.empty()) equipment[5] = actor->rightHandVariant.c_str();
    if (!actor->leftHandVariant.empty()) equipment[6] = actor->leftHandVariant.c_str();
    for (size_t index = 0; index < actor->specialVariants.size(); ++index)
        if (!actor->specialVariants[index].empty())
            equipment[index + 8] = actor->specialVariants[index].c_str();
    for (auto mode : {"nu", "wl", "a1", "gh", "dt", "dd"}) {
        auto weapon = monsterModeWeapon(archives, actor->token, mode, actor->baseWeapon);
        if (weapon.empty())
            throw std::runtime_error("Original Act I hireling COF is missing");
        auto animation = graphics_.composite("monsters", actor->token, mode, weapon, &equipment);
        if (animation.frames.empty() || !animation.completeComposite)
            throw std::runtime_error("Original Act I hireling animation is incomplete");
        hirelingAnimations.emplace(mode, std::move(animation));
    }
    if (actor->attack1Projectile &&
        !projectileAnimations.contains(actor->attack1Projectile->id)) {
        auto animation = graphics_.single(actor->attack1ProjectileArt);
        if (animation.frames.empty())
            throw std::runtime_error("Original Rogue hireling missile art is missing");
        projectileAnimations.emplace(actor->attack1Projectile->id, std::move(animation));
    }
}
} // namespace d2x
