#include "gameplay/skills/spec.hpp"
#include "presentation/scene_assets.hpp"
#include "content/classic_data.hpp"
#include <stdexcept>
#include <utility>

namespace d2x {
void SceneAssets::loadSkillIcons(Archives &archives, const ClassicData &content) {
    const auto loadPortrait = [&](const std::string &path) {
        if (path.empty() || petPortraits.contains(path)) return;
        auto icon = uiGraphics_.single(path, true);
        if (icon.frames.empty()) throw std::runtime_error("Missing original pet portrait: " + path);
        petPortraits.emplace(path, std::move(icon));
    };
    for (const auto &[type, entry] : content.petPortraits) {
        (void)type;
        if (!entry.hireling && !entry.iconType) continue;
        loadPortrait(entry.baseArt);
        for (const auto &[monsterClass, path] : entry.classArt) { (void)monsterClass; loadPortrait(path); }
    }
    const auto potionHint = content.itemStrings.find("hireiconinfo1"), inventoryHint = content.itemStrings.find("hireiconinfo2");
    if (potionHint != content.itemStrings.end()) hirelingPotionHint = potionHint->second;
    if (inventoryHint != content.itemStrings.end()) {
        hirelingInventoryHint = inventoryHint->second;
        // The current common input adapter binds the Hireling Inventory action to O.
        if (const auto key = hirelingInventoryHint.find("%s"); key != std::string::npos)
            hirelingInventoryHint.replace(key, 2, "O");
    }
    const auto &skills = content.tables.at("skills");
    auto descriptionTable = content.tables.find("skilldesc");
    const auto palette = archives.read("data/global/palette/sky/pal.pl2");
    constexpr size_t colorShifts = 0x6B600 + 13 * 3;
    if (palette.size() < colorShifts + 13 * 256)
        throw std::runtime_error("Original skill icon color transforms are missing");
    const auto transformed = [&](const auto &original, int cell, int color) {
        if (cell < 0 || size_t(cell) >= original.frames.size())
            throw std::runtime_error("Missing original skill icon color source");
        auto frame = original.frames[size_t(cell)];
        for (auto &pixel : frame.pixels)
            if (pixel) pixel = palette[colorShifts + size_t(color) * 256 + pixel];
        return uiGraphics_.upload(frame);
    };
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
        const auto *original = uiGraphics_.animation("data/global/ui/spells/skillicon.dc6");
        if (!original) throw std::runtime_error("Missing original common skill icon color source");
        attackUnavailableIcon = transformed(*original, *cell, 1);
        break;
    }
    if (!attackIcon.texture.id)
        throw std::runtime_error("Missing original Attack skill icon");
    const auto &genericFrames = sheet("").frames;
    const auto *genericOriginal = uiGraphics_.animation("data/global/ui/spells/skillicon.dc6");
    if (!genericOriginal) throw std::runtime_error("Missing original common skill icon color source");
    for (const auto &[id, entry] : content.skills.skills)
        if (entry.classCode.empty()) {
            if (entry.iconCell < 0 || size_t(entry.iconCell) >= genericFrames.size())
                throw std::runtime_error("Missing original common skill icon: " + entry.name);
            skillIcons.emplace(id, SkillIcon{genericFrames[entry.iconCell], entry.leftAllowed, {}, {},
                transformed(*genericOriginal, entry.iconCell, 1)});
        }
    for (const auto &tree : content.skills.classes) {
        const auto &frames = sheet(tree.iconToken).frames;
        const auto *original = uiGraphics_.animation("data/global/ui/spells/" + tree.iconToken + "skillicon.dc6");
        if (!original) throw std::runtime_error("Original skill tree icon color source is missing");
        for (const auto &[id, entry] : content.skills.skills) {
            if (entry.classCode != tree.classCode) continue;
            if (entry.iconCell < 0 || size_t(entry.iconCell) >= frames.size() ||
                size_t(entry.iconCell) >= original->frames.size())
                throw std::runtime_error("Missing original skill icon: " + entry.name);
            // Original D2Client RVA 78979: CelDrawColor, gray 5 / hover blue 3.
            // Picker RVA 3E770 receives the use-state transform; red is 1.
            skillIcons.emplace(id, SkillIcon{frames[entry.iconCell], entry.leftAllowed,
                transformed(*original, entry.iconCell, 5), transformed(*original, entry.iconCell, 3),
                transformed(*original, entry.iconCell, 1)});
        }
    }
}
} // namespace d2x
