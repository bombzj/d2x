#include "monster_spell_data.hpp"
#include <algorithm>
#include <cctype>

namespace d2x {
namespace {
std::string resolveMode(std::string_view source, const DataTable &sequences, int event = 2) {
    if (source == "SC") return "SC";
    for (size_t row = 0; row < sequences.rows().size(); ++row)
        if (sequences.value(row, "sequence") == source &&
            sequences.number(row, "event").value_or(0) == event)
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
std::optional<MonsterNest> loadMonsterNest(
    const DataTable &monsters, size_t monsterRow,
    const DataTable &skills, const DataTable &sequences) {
    const auto child = monsters.value(monsterRow, "spawn");
    const auto sourceSkill = monsters.value(monsterRow, "Skill1");
    const auto sequence = monsters.value(monsterRow, "Sk1mode");
    const auto mode = resolveMode(sequence, sequences, 4);
    if (child.empty() || sourceSkill.empty() || mode.empty()) return std::nullopt;
    for (size_t row = 0; row < skills.rows().size(); ++row)
        if (skills.value(row, "skill") == sourceSkill &&
            skills.number(row, "srvdofunc") == 91)
            return MonsterNest{std::string(sourceSkill), mode, std::string(child),
                               std::string(sequence)};
    return std::nullopt;
}
std::optional<MonsterWeb> loadMonsterWeb(
    const Archives &archives, const DataTable &monsters, size_t monsterRow,
    const DataTable &skills, const DataTable &missiles) {
    const auto sourceSkill = monsters.value(monsterRow, "Skill1");
    const auto sourceMode = monsters.value(monsterRow, "Sk1mode");
    if (sourceSkill.empty() || sourceMode != "A2") return std::nullopt;
    for (size_t skillRow = 0; skillRow < skills.rows().size(); ++skillRow) {
        if (skills.value(skillRow, "skill") != sourceSkill ||
            skills.number(skillRow, "srvdofunc") != 23 ||
            skills.value(skillRow, "aurastate") != "spiderlay" ||
            skills.value(skillRow, "auratargetstate") != "slowed" ||
            skills.value(skillRow, "aurastat1") != "velocitypercent") continue;
        const auto aura = skills.number(skillRow, "auralencalc");
        const auto slow = skills.number(skillRow, "calc4");
        const auto percent = skills.number(skillRow, "aurastatcalc1");
        if (!aura || *aura <= 0 || *aura > 10000 ||
            !slow || *slow <= 0 || *slow > 1000 ||
            !percent || *percent < -100 || *percent > 0) return std::nullopt;
        // D2Game SrvDo023 creates the spidergoo missile from the original table.
        for (size_t missileRow = 0; missileRow < missiles.rows().size(); ++missileRow) {
            if (missiles.value(missileRow, "Missile") != "spidergoo") continue;
            const auto id = missiles.number(missileRow, "Id");
            const auto range = missiles.number(missileRow, "Range");
            const auto size = missiles.number(missileRow, "Size");
            auto file = std::string(missiles.value(missileRow, "CelFile"));
            for (char &ch : file)
                ch = char(std::tolower(static_cast<unsigned char>(ch)));
            const auto art = "data/global/missiles/" + file + ".dcc";
            if (!id || *id < 0 || !range || *range <= 0 || *range > 10000 ||
                !size || *size <= 0 || *size > 16 || file.empty() ||
                !archives.contains(art)) return std::nullopt;
            return MonsterWeb{std::string(sourceSkill), std::string(sourceMode), art,
                              *id, float(*range) / 25.f, float(*size) / 2.f,
                              float(*aura) / 25.f, float(*slow) / 25.f, *percent};
        }
    }
    return std::nullopt;
}
} // namespace d2x
