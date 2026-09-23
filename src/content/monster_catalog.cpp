#include "monster_catalog.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
MonsterCatalog::MonsterCatalog(Archives &archives, const DataTable &stats) {
    if (!stats.has("Id") || !stats.has("hcIdx") || !stats.has("MonStatsEx")) {
        diagnostics_.push_back("Population disabled: this MonStats schema needs a version-specific adapter.");
        return;
    }
    for (auto file : {"monstats2", "monpreset", "superuniques", "monplace", "monumod"}) {
        auto path = std::string("data/global/excel/") + file + ".txt";
        if (!archives.contains(path))
            diagnostics_.push_back("Population disabled: missing " + path +
                                   "; rebuild the compact MPQ from mpq2.");
    }
    if (!diagnostics_.empty())
        return;
    DataTable extended(archives.read("data/global/excel/monstats2.txt"));
    std::optional<DataTable> levels;
    if (archives.contains("data/global/excel/monlvl.txt"))
        levels.emplace(archives.read("data/global/excel/monlvl.txt"));
    std::map<std::string, size_t, std::less<>> extendedRows;
    for (size_t row = 0; row < extended.rows().size(); ++row)
        extendedRows.emplace(extended.value(row, "Id"), row);
    for (size_t row = 0; row < stats.rows().size(); ++row) {
        if (stats.value(row, "hcIdx").empty())
            continue;
        auto n = [&](std::string_view field) { return stats.number(row, field).value_or(0); };
        MonsterRecord m;
        m.id = stats.value(row, "Id");
        m.index = n("hcIdx");
        m.base = stats.value(row, "BaseId");
        m.next = stats.value(row, "NextInClass");
        m.name = stats.value(row, "NameStr");
        m.token = stats.value(row, "Code");
        m.ai = stats.value(row, "AI");
        m.spawn = stats.value(row, "spawn");
        m.minions = {std::string(stats.value(row, "minion1")), std::string(stats.value(row, "minion2"))};
        m.rarity = n("Rarity");
        m.minGroup = n("MinGrp");
        m.maxGroup = n("MaxGrp");
        m.partyMin = n("PartyMin");
        m.partyMax = n("PartyMax");
        m.sparse = n("sparsePopulate");
        m.normalLevel = n("Level");
        m.walkVelocity = stats.number(row, "Velocity");
        if (m.walkVelocity && (*m.walkVelocity < 0 || *m.walkVelocity > 255))
            throw std::runtime_error("Unsupported monster Velocity: " + m.id);
        if (auto attack = stats.number(row, "A1TH"); attack && *attack >= 0 && m.normalLevel > 0) {
            if (n("noRatio"))
                m.normalAttackRating = *attack;
            else if (levels && !levels->rows().empty()) {
                const auto levelRow = std::min(size_t(m.normalLevel), levels->rows().size() - 1);
                if (auto base = levels->number(levelRow, "L-TH"); base && *base >= 0) {
                    const auto rating = int64_t(*base) * *attack / 100;
                    if (rating > std::numeric_limits<int>::max())
                        throw std::runtime_error("Monster attack rating overflow: " + m.id);
                    m.normalAttackRating = int(rating);
                }
            }
        }
        if (auto armor = stats.number(row, "AC"); armor && *armor >= 0 && m.normalLevel > 0) {
            if (n("noRatio"))
                m.normalDefense = *armor;
            else if (levels && !levels->rows().empty()) {
                const auto levelRow = std::min(size_t(m.normalLevel), levels->rows().size() - 1);
                if (auto base = levels->number(levelRow, "L-AC"); base && *base >= 0) {
                    const auto defense = int64_t(*base) * *armor / 100;
                    if (defense > std::numeric_limits<int>::max())
                        throw std::runtime_error("Monster defense overflow: " + m.id);
                    m.normalDefense = int(defense);
                }
            }
        }
        m.alignment = n("Align");
        m.enabled = n("enabled") != 0;
        m.randomSpawn = n("isSpawn") != 0;
        m.ranged = n("rangedtype") != 0;
        m.placeSpawn = n("placespawn") != 0;
        m.killable = n("killable") != 0;
        m.npc = n("npc") != 0;
        m.boss = n("boss") != 0 || n("primeevil") != 0;
        auto extra = extendedRows.find(stats.value(row, "MonStatsEx"));
        if (extra == extendedRows.end())
            throw std::runtime_error("Missing MonStats2 record for " + m.id);
        m.critter = extended.number(extra->second, "critter").value_or(0) != 0;
        m.inert = extended.number(extra->second, "inert").value_or(0) != 0;
        if (!indices_.emplace(m.index, m.id).second)
            throw std::runtime_error("Duplicate MonStats hcIdx: " + std::to_string(m.index));
        if (!monsters_.emplace(m.id, m).second) {
            // Some shipped tables repeat Act V names (e.g. cr_lancer8). Do not
            // guess which native row a string reference meant, or block Act I.
            ambiguous_.insert(m.id);
            diagnostics_.push_back("Ambiguous duplicate MonStats Id disabled: " + m.id);
        }
    }
    DataTable uniques(archives.read("data/global/excel/superuniques.txt"));
    for (size_t row = 0; row < uniques.rows().size(); ++row) {
        if (uniques.value(row, "hcIdx").empty())
            continue;
        SuperUniqueRecord u;
        u.id = uniques.value(row, "Superunique");
        u.name = uniques.value(row, "Name");
        u.monster = uniques.value(row, "Class");
        u.index = uniques.number(row, "hcIdx").value();
        u.minGroup = uniques.number(row, "MinGrp").value_or(0);
        u.maxGroup = uniques.number(row, "MaxGrp").value_or(0);
        for (int i = 0; i < 3; ++i)
            u.modifiers[i] = uniques.number(row, "Mod" + std::to_string(i + 1)).value_or(0);
        u.treasureClasses = {std::string(uniques.value(row, "TC")), std::string(uniques.value(row, "TC(N)")),
                             std::string(uniques.value(row, "TC(H)"))};
        if (!find(u.monster) || !uniques_.emplace(u.id, u).second)
            throw std::runtime_error("Invalid SuperUniques record: " + u.id);
    }
    DataTable places(archives.read("data/global/excel/monplace.txt"));
    for (size_t row = 0; row < places.rows().size(); ++row)
        places_.emplace(places.value(row, "code"));
    DataTable presets(archives.read("data/global/excel/monpreset.txt"));
    for (size_t row = 0; row < presets.rows().size(); ++row) {
        auto act = presets.number(row, "Act");
        if (!act)
            continue;
        if (*act < 1 || *act > 5)
            throw std::runtime_error("Invalid MonPreset Act");
        // Native DS1 IDs are zero-based within each act; duplicates are intentional.
        presets_[*act - 1].emplace_back(presets.value(row, "Place"));
    }
    DataTable modifiers(archives.read("data/global/excel/monumod.txt"));
    championChance_ = modifiers.number(0, "constants").value_or(0);
    if (championChance_ < 0 || championChance_ > 100)
        throw std::runtime_error("Invalid MonUMod champion chance");
    supported_ = true;
}
const MonsterRecord *MonsterCatalog::find(std::string_view id) const {
    if (ambiguous_.contains(id))
        return nullptr;
    auto it = monsters_.find(id);
    return it == monsters_.end() ? nullptr : &it->second;
}
const SuperUniqueRecord *MonsterCatalog::superUnique(std::string_view id) const {
    auto it = uniques_.find(id);
    return it == uniques_.end() ? nullptr : &it->second;
}
MonsterPreset MonsterCatalog::preset(int act, int index, int version) const {
    if (version <= 4) {
        auto it = indices_.find(index);
        return it == indices_.end() ? MonsterPreset{} : MonsterPreset{MonsterPresetKind::Monster, it->second};
    }
    if (act < 0 || act >= 5 || index < 0 || size_t(index) >= presets_[act].size())
        return {};
    const auto &id = presets_[act][index];
    // Same linker priority as the original table loader; case is significant here.
    if (superUnique(id))
        return {MonsterPresetKind::SuperUnique, id};
    if (find(id))
        return {MonsterPresetKind::Monster, id};
    if (places_.contains(id))
        return {MonsterPresetKind::Place, id};
    return {MonsterPresetKind::Unknown, id};
}
} // namespace d2x
