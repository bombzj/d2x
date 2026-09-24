#include "monster_spell_data.hpp"
#include <algorithm>
#include <cctype>

namespace d2x {
std::array<std::optional<MonsterSpell>, 4> loadMonsterSpells(
    Archives &archives, const DataTable &monsters, size_t monsterRow,
    const DataTable &skills, const DataTable &missiles) {
    std::array<std::optional<MonsterSpell>, 4> result;
    for (int slot = 0; slot < 4; ++slot) {
        const auto sourceSkill = monsters.value(monsterRow, "Skill" + std::to_string(slot + 1));
        const auto sourceMode = monsters.value(monsterRow, "Sk" + std::to_string(slot + 1) + "mode");
        if (sourceSkill.empty() || sourceMode != "SC") continue;
        for (size_t skillRow = 0; skillRow < skills.rows().size(); ++skillRow) {
            if (skills.value(skillRow, "skill") != sourceSkill) continue;
            const auto missileName = skills.value(skillRow, "srvmissile");
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
                        std::string(sourceMode), art, std::string(element),
                        MonsterProjectile{*id, float(*velocity), float(*range) / 25.f},
                        *minimum, *maximum};
                break;
            }
            break;
        }
    }
    return result;
}
} // namespace d2x
