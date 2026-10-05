#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "presentation/scene_assets.hpp"
#include "content/monsters/monster_animation.hpp"
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include <set>
#include <stdexcept>

namespace d2x {
void SceneAssets::loadHirelingAnimations(Archives &archives, const GameSession &session) {
    const DataTable pets(archives.read("data/global/excel/pettype.txt"));
    size_t pet = 0;
    while (pet < pets.rows().size() && pets.value(pet, "pet type") != "hireable") ++pet;
    if (pet == pets.rows().size()) throw std::runtime_error("Original hireable PetType is missing");
    std::set<int> loaded;
    for (const auto &entry : session.content().hirelings)
        if ((entry.act == 1 || entry.act == 2 || entry.act == 3 || entry.act == 5) && loaded.insert(entry.classId).second) {
    const MonsterRecord *actor = nullptr;
    for (const auto &[id, record] : session.monsterContent().monsters())
        if (record.index == entry.classId) {
            actor = &record;
            break;
        }
    if (!actor)
        throw std::runtime_error("Original hireling actor is missing");
    std::string icon(pets.value(pet, "baseicon"));
    for (int index = 1; index <= 4; ++index)
        if (pets.number(pet, "mclass" + std::to_string(index)) == actor->index)
            icon = pets.value(pet, "micon" + std::to_string(index));
    auto portrait = uiGraphics_.single("data/global/ui/hireables/" + icon + ".dc6");
    if (portrait.frames.empty()) throw std::runtime_error("Original hireling portrait is missing");
    hirelingPortraits.emplace(actor->index, std::move(portrait));
    std::array<const char *, 16> equipment;
    equipment.fill("");
    for (size_t index = 0; index < actor->components.size(); ++index)
        if (!actor->components[index].empty())
            equipment[index] = actor->components[index].c_str();
    for (auto mode : {"nu", "wl", "a1", "gh", "dt", "dd", "sc"}) {
        if (std::string_view(mode) == "sc" && !actor->castMode) continue;
        auto weapon = monsterModeWeapon(archives, actor->token, mode, actor->baseWeapon);
        if (weapon.empty())
            throw std::runtime_error("Original hireling COF is missing");
        auto modeEquipment = equipment;
        std::array<std::string, 16> inferredVariants;
        constexpr const char *codes[]{"hd", "tr", "lg", "ra", "la", "rh", "lh", "sh", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"};
        for (size_t component = 0; component < inferredVariants.size(); ++component) {
            if (!actor->components[component].empty()) continue;
            // Some shipped death COFs include a blood layer without a MonStats2
            // variant. Accept only a unique original component file, never guess.
            const std::string prefix = actor->token + codes[component];
            const std::string suffix = std::string(mode) + weapon + ".dcc";
            const auto files = archives.list("data/global/monsters/" + actor->token + "/" + codes[component] + "/" + prefix + "*" + suffix);
            if (files.size() != 1) continue;
            const auto start = files.front().find_last_of("/\\") + 1 + prefix.size();
            inferredVariants[component] = files.front().substr(start, files.front().size() - start - suffix.size());
            modeEquipment[component] = inferredVariants[component].c_str();
        }
        auto animation = graphics_.composite("monsters", actor->token, mode, weapon, &modeEquipment);
        if (animation.frames.empty() || !animation.completeComposite)
            throw std::runtime_error("Original hireling animation is incomplete");
        hirelingAnimations.emplace(std::to_string(actor->index) + "/" + mode, std::move(animation));
    }
    if (actor->attack1Projectile &&
        !projectileAnimations.contains(actor->attack1Projectile->id)) {
        auto animation = graphics_.single(actor->attack1ProjectileArt);
        if (animation.frames.empty())
            throw std::runtime_error("Original Rogue hireling missile art is missing");
        projectileAnimations.emplace(actor->attack1Projectile->id, std::move(animation));
    }
    }
}
} // namespace d2x
