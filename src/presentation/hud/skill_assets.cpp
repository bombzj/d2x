#include "gameplay/skills/spec.hpp"
#include "presentation/scene_assets.hpp"
#include "content/classic_data.hpp"

namespace d2x {
void SceneAssets::loadSkillIcons(Archives &archives, const ClassicData &content) {
    for (const auto &[id, entry] : content.skills.skills)
        if (entry.spell && entry.spell->summon && !entry.spell->summon->iconArt.empty()) {
            auto icon = uiGraphics_.single(entry.spell->summon->iconArt);
            if (icon.frames.empty()) throw std::runtime_error("Missing original summon portrait");
            summonPortraits.emplace(id, std::move(icon));
        }
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
            skillIcons.emplace(id, SkillIcon{genericFrames[entry.iconCell], entry.leftAllowed, {}, {}});
        }
    for (const auto &tree : content.skills.classes) {
        const auto &frames = sheet(tree.iconToken).frames;
        const auto *original = uiGraphics_.animation("data/global/ui/spells/" + tree.iconToken + "skillicon.dc6");
        const auto palette = archives.read("data/global/palette/sky/pal.pl2");
        constexpr size_t colorShifts = 0x6B600 + 13 * 3;
        if (!original || palette.size() < colorShifts + 13 * 256)
            throw std::runtime_error("Original skill tree icon color transforms are missing");
        for (const auto &[id, entry] : content.skills.skills) {
            if (entry.classCode != tree.classCode) continue;
            if (entry.iconCell < 0 || size_t(entry.iconCell) >= frames.size() ||
                size_t(entry.iconCell) >= original->frames.size())
                throw std::runtime_error("Missing original skill icon: " + entry.name);
            const auto transformed = [&](int color) {
                auto frame = original->frames[size_t(entry.iconCell)];
                for (auto &pixel : frame.pixels)
                    if (pixel) pixel = palette[colorShifts + size_t(color) * 256 + pixel];
                return uiGraphics_.upload(frame);
            };
            // Original D2Client RVA 78979: CelDrawColor, gray 5 / hover blue 3.
            skillIcons.emplace(id, SkillIcon{frames[entry.iconCell], entry.leftAllowed, transformed(5), transformed(3)});
        }
    }
}
} // namespace d2x
