#include "scene_assets.hpp"
#include <optional>

namespace d2x {
void SceneAssets::loadSkillIcons(Archives &archives, const ClassicData &content) {
    const auto &skills = content.tables.at("skills");
    std::optional<DataTable> descriptions;
    if (skills.has("skilldesc"))
        descriptions.emplace(archives.read("data/global/excel/skilldesc.txt"));
    std::map<std::string, GpuAnimation> sheets;
    auto load = [&](std::string_view name) -> SkillIcon {
        for (size_t row = 0; row < skills.rows().size(); ++row) {
            if (skills.value(row, "skill") != name)
                continue;
            int cell = skills.number(row, "IconCel").value_or(-1);
            if (descriptions) {
                cell = -1;
                auto key = skills.value(row, "skilldesc");
                for (size_t description = 0; description < descriptions->rows().size(); ++description)
                    if (descriptions->value(description, "skilldesc") == key) {
                        cell = descriptions->number(description, "IconCel").value_or(-1);
                        break;
                    }
            }
            auto character = skills.value(row, "charclass");
            std::string sheet = character == "bar" ? "ba" : character == "sor" ? "so" : "";
            auto path = "data/global/ui/spells/" + sheet + "skillicon.dc6";
            if (!sheets.contains(path))
                sheets.emplace(path, uiGraphics_.single(path));
            const auto &frames = sheets.at(path).frames;
            if (cell < 0 || size_t(cell) >= frames.size())
                throw std::runtime_error("Missing original skill icon: " + std::string(name));
            return {frames[cell], skills.number(row, "leftskill").value_or(0) != 0};
        }
        throw std::runtime_error("Missing Skills record: " + std::string(name));
    };
    attackIcon = load("Attack").sprite;
    const std::array<const char *, skillCount> names{"Fire Ball", "Frost Nova",  "Whirlwind",
                                                     "Teleport",  "Leap Attack", "War Cry"};
    for (size_t i = 0; i < names.size(); ++i)
        skillIcons[i] = load(names[i]);
}
} // namespace d2x
