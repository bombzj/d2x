#include "scene_assets.hpp"

namespace d2x {
void SceneAssets::loadSkillIcons(Archives &, const ClassicData &content) {
    const auto &skills = content.tables.at("skills");
    auto descriptionTable = content.tables.find("skilldesc");
    std::map<std::string, GpuAnimation> sheets;
    auto sheet = [&](const std::string &token) -> const GpuAnimation & {
        auto path = "data/global/ui/spells/" + token + "skillicon.dc6";
        auto found = sheets.find(path);
        if (found == sheets.end())
            found = sheets.emplace(path, uiGraphics_.single(path)).first;
        if (found->second.frames.empty())
            throw std::runtime_error("Missing original MPQ skill icon sheet: " + path);
        return found->second;
    };
    for (size_t row = 0; row < skills.rows().size(); ++row) {
        if (skills.value(row, "skill") != "Attack") continue;
        auto key = skills.value(row, "skilldesc");
        auto cell = skills.number(row, "IconCel");
        if (descriptionTable != content.tables.end())
            for (size_t index = 0; index < descriptionTable->second.rows().size(); ++index)
                if (descriptionTable->second.value(index, "skilldesc") == key) {
                    cell = descriptionTable->second.number(index, "IconCel");
                    break;
                }
        const auto &frames = sheet("").frames;
        if (!cell || *cell < 0 || size_t(*cell) >= frames.size())
            throw std::runtime_error("Missing original Attack icon frame");
        attackIcon = frames[*cell];
        break;
    }
    if (!attackIcon.texture.id)
        throw std::runtime_error("Missing original Attack skill icon");
    const auto &genericFrames = sheet("").frames;
    for (const auto &[id, entry] : content.skills.skills)
        if (entry.classCode.empty()) {
            if (entry.iconCell < 0 || size_t(entry.iconCell) >= genericFrames.size())
                throw std::runtime_error("Missing original common skill icon: " + entry.name);
            skillIcons.emplace(id, SkillIcon{genericFrames[entry.iconCell], entry.leftAllowed});
        }
    for (const auto &tree : content.skills.classes) {
        const auto &frames = sheet(tree.iconToken).frames;
        for (const auto &[id, entry] : content.skills.skills) {
            if (entry.classCode != tree.classCode) continue;
            if (size_t(entry.iconCell) >= frames.size())
                throw std::runtime_error("Missing original skill icon: " + entry.name);
            skillIcons.emplace(id, SkillIcon{frames[entry.iconCell], entry.leftAllowed});
        }
    }
}
} // namespace d2x
