#include "sorceress_data.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace d2x {
namespace {
int required(const DataTable &table, size_t row, std::string_view field) {
    auto value = table.number(row, field);
    if (!value) throw std::runtime_error("Missing original sorceress field: " + std::string(field));
    return *value;
}
std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return result;
}
} // namespace
void loadSorceressEffects(SkillCatalog &catalog, const DataTable &skills,
                          const DataTable &missiles, const DataTable &overlays,
                          const DataTable &sounds, Archives &archives) {
    constexpr struct { std::string_view name; Skill effect; } supported[] = {
        {"Teleport", Skill::Teleport}, {"Fire Bolt", Skill::FireBolt},
        {"Fire Ball", Skill::Fireball}, {"Frost Nova", Skill::FrostNova},
        {"Ice Bolt", Skill::IceBolt}, {"Nova", Skill::Nova},
        {"Ice Blast", Skill::IceBlast},
        {"Static Field", Skill::StaticField}};
    const auto warmth = std::find_if(catalog.skills.begin(), catalog.skills.end(),
        [](const auto &pair) { return pair.second.classCode == "sor" &&
            pair.second.sourceName == "Warmth"; });
    if (warmth == catalog.skills.end() || !warmth->second.passive)
        throw std::runtime_error("Original Warmth passive is missing");
    size_t warmthRow = 0;
    for (; warmthRow < skills.rows().size(); ++warmthRow)
        if (skills.number(warmthRow, "Id") == warmth->first) break;
    if (warmthRow == skills.rows().size() ||
        skills.value(warmthRow, "passivestat1") != "manarecoverybonus" ||
        skills.value(warmthRow, "passivecalc1") != "ln12")
        throw std::runtime_error("Unsupported original Warmth passive");
    warmth->second.manaRecoveryPerRank = std::pair{required(skills, warmthRow, "Param1"),
                                                   required(skills, warmthRow, "Param2")};
    const auto mastery = std::find_if(catalog.skills.begin(), catalog.skills.end(),
        [](const auto &pair) { return pair.second.classCode == "sor" &&
            pair.second.sourceName == "Fire Mastery"; });
    if (mastery == catalog.skills.end() || !mastery->second.passive)
        throw std::runtime_error("Original Fire Mastery passive is missing");
    size_t masteryRow = 0;
    for (; masteryRow < skills.rows().size(); ++masteryRow)
        if (skills.number(masteryRow, "Id") == mastery->first) break;
    if (masteryRow == skills.rows().size() ||
        skills.value(masteryRow, "passivestat1") != "passive_fire_mastery" ||
        skills.value(masteryRow, "passivecalc1") != "ln12")
        throw std::runtime_error("Unsupported original Fire Mastery passive");
    mastery->second.fireMasteryPerRank = std::pair{required(skills, masteryRow, "Param1"),
                                                  required(skills, masteryRow, "Param2")};
    const auto lightningMastery = std::find_if(catalog.skills.begin(), catalog.skills.end(),
        [](const auto &pair) { return pair.second.classCode == "sor" &&
            pair.second.sourceName == "Lightning Mastery"; });
    if (lightningMastery == catalog.skills.end() || !lightningMastery->second.passive)
        throw std::runtime_error("Original Lightning Mastery passive is missing");
    size_t lightningMasteryRow = 0;
    for (; lightningMasteryRow < skills.rows().size(); ++lightningMasteryRow)
        if (skills.number(lightningMasteryRow, "Id") == lightningMastery->first) break;
    if (lightningMasteryRow == skills.rows().size() ||
        skills.value(lightningMasteryRow, "passivestat1") != "passive_ltng_mastery" ||
        skills.value(lightningMasteryRow, "passivecalc1") != "ln12")
        throw std::runtime_error("Unsupported original Lightning Mastery passive");
    lightningMastery->second.lightningMasteryPerRank = std::pair{
        required(skills, lightningMasteryRow, "Param1"),
        required(skills, lightningMasteryRow, "Param2")};
    const auto coldMastery = std::find_if(catalog.skills.begin(), catalog.skills.end(),
        [](const auto &pair) { return pair.second.classCode == "sor" &&
            pair.second.sourceName == "Cold Mastery"; });
    if (coldMastery == catalog.skills.end() || !coldMastery->second.passive)
        throw std::runtime_error("Original Cold Mastery passive is missing");
    size_t coldMasteryRow = 0;
    for (; coldMasteryRow < skills.rows().size(); ++coldMasteryRow)
        if (skills.number(coldMasteryRow, "Id") == coldMastery->first) break;
    if (coldMasteryRow == skills.rows().size() ||
        skills.value(coldMasteryRow, "passivestat1") != "passive_cold_pierce" ||
        skills.value(coldMasteryRow, "passivecalc1") != "ln12")
        throw std::runtime_error("Unsupported original Cold Mastery passive");
    coldMastery->second.coldPiercePerRank = std::pair{
        required(skills, coldMasteryRow, "Param1"),
        required(skills, coldMasteryRow, "Param2")};
    for (const auto &[name, effect] : supported) {
        const SkillRecord *record = nullptr;
        for (const auto &[id, entry] : catalog.skills)
            if (entry.classCode == "sor" && entry.sourceName == name) record = &entry;
        if (!record) throw std::runtime_error("Original sorceress skill is missing: " + std::string(name));
        size_t row = 0;
        for (; row < skills.rows().size(); ++row)
            if (skills.number(row, "Id") == record->id) break;
        if (row == skills.rows().size()) throw std::runtime_error("Original sorceress skill row is missing");
        OriginalSkillSpec spec;
        spec.effect = effect;
        spec.mana = required(skills, row, "mana");
        spec.minimumMana = required(skills, row, "minmana");
        spec.manaPerLevel = required(skills, row, "lvlmana");
        spec.manaShift = required(skills, row, "manashift");
        spec.hitShift = required(skills, row, "HitShift");
        spec.fireDamage = skills.value(row, "EType") == "fire";
        spec.lightningDamage = skills.value(row, "EType") == "ltng";
        if ((effect == Skill::FireBolt || effect == Skill::Fireball) && !spec.fireDamage)
            throw std::runtime_error("Unsupported original fire spell element");
        const auto soundName = skills.value(row, "stsound");
        size_t soundRow = 0;
        for (; soundRow < sounds.rows().size(); ++soundRow)
            if (sounds.value(soundRow, "Sound") == soundName) break;
        if (soundName.empty() || soundRow == sounds.rows().size())
            throw std::runtime_error("Missing original sorceress cast sound: " + std::string(name));
        spec.castSoundArt = "data/global/sfx/" +
            std::string(sounds.value(soundRow, "FileName"));
        if (!archives.contains(spec.castSoundArt))
            throw std::runtime_error("Missing original sorceress cast sound art: " + std::string(name));
        if (effect == Skill::Teleport) {
            size_t overlayRow = 0;
            for (; overlayRow < overlays.rows().size(); ++overlayRow)
                if (overlays.value(overlayRow, "overlay") == "teleport") break;
            if (overlayRow == overlays.rows().size())
                throw std::runtime_error("Original Teleport overlay is missing");
            auto file = lower(overlays.value(overlayRow, "Filename"));
            spec.visualFrames = required(overlays, overlayRow, "Frames");
            spec.visualArt = "data/global/overlays/" + file + ".dcc";
            if (file.empty() || spec.visualFrames <= 0 || !archives.contains(spec.visualArt))
                throw std::runtime_error("Original Teleport overlay art is missing");
        } else if (effect == Skill::StaticField) {
            if (skills.value(row, "calc1") != "par4" ||
                skills.value(row, "aurarangecalc") != "ln12")
                throw std::runtime_error("Unsupported original Static Field formula");
            spec.staticPercent = required(skills, row, "Param4");
            spec.staticRange = required(skills, row, "Param1");
            spec.staticRangePerLevel = required(skills, row, "Param2");
        } else if (effect != Skill::Teleport) {
            spec.minimumDamage = required(skills, row, "EMin");
            spec.maximumDamage = required(skills, row, "EMax");
            for (int index = 0; index < 5; ++index) {
                const auto level = std::to_string(index + 1);
                spec.minimumPerLevel[index] = required(skills, row, "EMinLev" + level);
                spec.maximumPerLevel[index] = required(skills, row, "EMaxLev" + level);
            }
            spec.coldFrames = skills.number(row, "ELen").value_or(0);
            if (spec.coldFrames > 0)
                for (int index = 0; index < 3; ++index)
                    spec.coldFramesPerLevel[index] = required(skills, row, "ELevLen" + std::to_string(index + 1));
            const auto formula = skills.value(row, "EDmgSymPerCalc");
            if (!formula.empty()) {
                const auto marker = formula.find("*par8");
                if (marker == std::string_view::npos || marker + 5 != formula.size())
                    throw std::runtime_error("Unsupported original damage synergy: " + std::string(name));
                spec.synergyPercent = required(skills, row, "Param8");
                size_t cursor = 0;
                while ((cursor = formula.find("skill('", cursor)) != std::string_view::npos) {
                    cursor += 7;
                    const auto end = formula.find("'.blvl)", cursor);
                    if (end == std::string_view::npos)
                        throw std::runtime_error("Unsupported original damage synergy target");
                    const auto target = formula.substr(cursor, end - cursor);
                    auto found = std::find_if(catalog.skills.begin(), catalog.skills.end(),
                        [&](const auto &pair) { return pair.second.classCode == "sor" &&
                            pair.second.sourceName == target; });
                    if (found == catalog.skills.end())
                        throw std::runtime_error("Missing original synergy skill: " + std::string(target));
                    spec.synergySkills.push_back(found->first);
                    cursor = end + 7;
                }
                if (spec.synergySkills.empty())
                    throw std::runtime_error("Original damage synergy has no supported target");
            }
            auto missileName = skills.value(row, "srvmissile");
            if (effect == Skill::FrostNova || effect == Skill::Nova)
                missileName = skills.value(row, "srvmissilea");
            size_t missileRow = 0;
            for (; missileRow < missiles.rows().size(); ++missileRow)
                if (missiles.value(missileRow, "Missile") == missileName) break;
            if (missileName.empty() || missileRow == missiles.rows().size())
                throw std::runtime_error("Missing original sorceress missile: " + std::string(name));
            spec.missileId = required(missiles, missileRow, "Id");
            if (effect == Skill::IceBlast &&
                (skills.value(row, "EType") != "cold" ||
                 required(missiles, missileRow, "pSrvDmgFunc") != 4 ||
                 required(missiles, missileRow, "CollideKill") != 1))
                throw std::runtime_error("Unsupported original Ice Blast missile rules");
            spec.missileVelocity = float(required(missiles, missileRow, "Vel"));
            spec.missileLifetime = float(required(missiles, missileRow, "Range")) / 25.f;
            spec.impactRadius = float(missiles.number(missileRow, "sHitPar1").value_or(0));
            if (effect == Skill::Nova &&
                (skills.value(row, "EType") != "ltng" ||
                 required(missiles, missileRow, "NextHit") != 1 ||
                 required(missiles, missileRow, "NextDelay") <= 0))
                throw std::runtime_error("Unsupported original Nova missile rules");
            if (effect == Skill::Nova)
                spec.missileNextDelay = required(missiles, missileRow, "NextDelay");
            auto file = lower(missiles.value(missileRow, "CelFile"));
            spec.missileArt = "data/global/missiles/" + file + ".dcc";
            if (file.empty() || !archives.contains(spec.missileArt))
                throw std::runtime_error("Missing original sorceress missile art: " + std::string(name));
        }
        catalog.skills.at(record->id).originalEffect = std::move(spec);
    }
}
} // namespace d2x
