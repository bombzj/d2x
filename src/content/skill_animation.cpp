#include "skill_animation.hpp"
#include "resources/anim_data.hpp"
#include <cctype>
#include <set>

namespace d2x {
void loadSkillAnimations(SkillCatalog &catalog, const DataTable &weapons, Archives &archives) {
    const AnimDataTable animations(archives.read("data/global/animdata.d2"));
    std::set<std::string> classes{"hth", "1js", "1jt", "1ss", "1st", "ht2"};
    for (size_t row = 0; row < weapons.rows().size(); ++row)
        for (auto field : {"wclass", "2handedwclass"}) {
            std::string value(weapons.value(row, field));
            for (auto &letter : value) letter = char(std::tolower(static_cast<unsigned char>(letter)));
            if (!value.empty()) classes.insert(value);
        }
    std::set<std::string> modes{"sc"};
    for (const auto &[id, skill] : catalog.skills)
        if (skill.basicAction != BasicSkillAction::None) modes.insert(skill.animationMode);
    for (const auto &tree : catalog.classes)
        for (const auto &weapon : classes)
            for (const auto &mode : modes) {
                const auto key = tree.iconToken + mode + weapon;
                auto upper = key;
                for (auto &letter : upper) letter = char(std::toupper(static_cast<unsigned char>(letter)));
                const auto *anim = animations.find(upper);
                if (!anim || anim->frames <= 1 || anim->frames > 144 || anim->speed <= 0 ||
                    !archives.contains("data/global/chars/" + tree.iconToken + "/cof/" + key + ".cof")) continue;
                // Native frame events: 1 = melee/skill, 2 = missile release.
                for (int frame = 0; frame < int(anim->frames); ++frame)
                    if (anim->frameFlags[size_t(frame)] == 1 || anim->frameFlags[size_t(frame)] == 2) {
                        auto &target = mode == "sc" ? catalog.castTimings : catalog.attackTimings;
                        target.emplace(key, SkillCatalog::CastTiming{int(anim->frames), anim->speed, frame});
                        break;
                    }
            }
}
} // namespace d2x
