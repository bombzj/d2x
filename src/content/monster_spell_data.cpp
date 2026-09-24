#include "monster_spell_data.hpp"
#include <algorithm>
#include <cctype>

namespace d2x {
namespace {
std::string resolveMode(std::string_view source, const DataTable &sequences) {
    if (source == "SC") return "SC";
    for (size_t row = 0; row < sequences.rows().size(); ++row)
        if (sequences.value(row, "sequence") == source &&
            sequences.number(row, "event").value_or(0) == 2)
            return std::string(sequences.value(row, "mode"));
    return {};
}
} // namespace
std::array<std::optional<MonsterSpell>, 4> loadMonsterSpells(
    Archives &archives, const DataTable &monsters, size_t monsterRow,
    const DataTable &skills, const DataTable &missiles, const DataTable &sequences) {
    std::array<std::optional<MonsterSpell>, 4> result;
    for (int slot = 0; slot < 4; ++slot) {
        const auto sourceSkill = monsters.value(monsterRow, "Skill" + std::to_string(slot + 1));
        const auto sourceMode = monsters.value(monsterRow, "Sk" + std::to_string(slot + 1) + "mode");
        const auto actionMode = resolveMode(sourceMode, sequences);
        if (sourceSkill.empty() || actionMode.empty()) continue;
        for (size_t skillRow = 0; skillRow < skills.rows().size(); ++skillRow) {
            if (skills.value(skillRow, "skill") != sourceSkill) continue;
            auto missileName = skills.value(skillRow, "srvmissile");
            if (missileName.empty()) missileName = skills.value(skillRow, "srvmissilea");
            if (missileName.empty()) break;
            for (size_t missileRow = 0; missileRow < missiles.rows().size(); ++missileRow) {
                if (missiles.value(missileRow, "Missile") != missileName) continue;
                const auto id = missiles.number(missileRow, "Id");
                const auto velocity = missiles.number(missileRow, "Vel");
                const auto range = missiles.number(missileRow, "Range");
                const auto minimum = missiles.number(missileRow, "EMin");
                const auto maximum = missiles.number(missileRow, "Emax");
                const auto element = missiles.value(missileRow, "EType");
                std::string file(missiles.value(missileRow, "CelFile"));
                std::transform(file.begin(), file.end(), file.begin(),
                               [](unsigned char ch) { return char(std::tolower(ch)); });
                const std::string art = "data/global/missiles/" + file + ".dcc";
                if (id && *id >= 0 && velocity && *velocity > 0 && range && *range > 0 &&
                    minimum && *minimum >= 0 && maximum && *maximum >= *minimum &&
                    *maximum <= 1000000 && (element == "fire" || element == "ltng" ||
                    element == "cold" || element == "mag") && !file.empty() &&
                    archives.contains(art))
                    result[size_t(slot)] = MonsterSpell{std::string(sourceSkill),
                        actionMode, art, std::string(element),
                        MonsterProjectile{*id, float(*velocity), float(*range) / 25.f},
                        *minimum, *maximum};
                break;
            }
            break;
        }
    }
    return result;
}
std::optional<MonsterResurrection> loadMonsterResurrection(
    const DataTable &monsters, size_t monsterRow,
    const DataTable &skills, const DataTable &sequences) {
    const auto sourceSkill = monsters.value(monsterRow, "Skill1");
    const auto mode = resolveMode(monsters.value(monsterRow, "Sk1mode"), sequences);
    if (sourceSkill.empty() || mode.empty()) return std::nullopt;
    for (size_t row = 0; row < skills.rows().size(); ++row)
        if (skills.value(row, "skill") == sourceSkill &&
            skills.number(row, "srvdofunc") == 97)
            return MonsterResurrection{std::string(sourceSkill), mode};
    return std::nullopt;
}
} // namespace d2x
