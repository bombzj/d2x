#include "skill_data.hpp"
#include "skill_eligibility.hpp"
#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>
#include <stdexcept>

namespace d2x {
namespace {
std::string normalized(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return char(std::tolower(ch)); });
    return result;
}
std::string skillText(std::string_view value) {
    // Native skill text has its bottom line first, like the SkillDesc lines.
    std::vector<std::string> lines;
    size_t start = 0;
    do {
        const auto end = value.find('\n', start);
        auto line = std::string(value.substr(start, end == std::string_view::npos ? end : end - start));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string_view::npos) break;
        start = end + 1;
    } while (start <= value.size());
    std::string result;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (it != lines.rbegin()) result += '\n';
        result += *it;
    }
    return result;
}
}
const SkillRecord *SkillCatalog::find(int id) const {
    auto found = skills.find(id);
    return found == skills.end() ? nullptr : &found->second;
}
const ClassSkillTree *SkillCatalog::tree(std::string_view code) const {
    auto found = std::find_if(classes.begin(), classes.end(),
        [code](const auto &entry) { return entry.classCode == code; });
    return found == classes.end() ? nullptr : &*found;
}
SkillCatalog loadSkillCatalog(const DataTable &skills, const DataTable &descriptions,
                              const DataTable &characterStats,
                              const std::vector<CharacterDefinition> &characters,
                              const ClassicStrings &strings) {
    SkillCatalog catalog;
    catalog.currentLevelLabel = strings.find("StrSkill2");
    catalog.nextLevelLabel = strings.find("StrSkill1");
    catalog.firstLevelLabel = strings.find("StrSkill17");
    if (catalog.currentLevelLabel.empty() || catalog.nextLevelLabel.empty() || catalog.firstLevelLabel.empty())
        throw std::runtime_error("Missing original skill level description");
    // Native tree headings use StrSklTree fragments, not CharStats' item
    // bonus format strings (StrSkillTab). Text and line breaks come from TBL.
    constexpr struct { const char *code, *icon, *background; int headings[3][3]; } art[] = {
        {"ama", "am", "a", {{10,11,4},{8,9,4},{6,7,4}}},
        {"sor", "so", "s", {{25,5,0},{24,5,0},{23,5,0}}},
        {"nec", "ne", "n", {{19,0,0},{17,18,5},{16,5,0}}},
        {"pal", "pa", "p", {{15,4,0},{14,13,0},{12,13,0}}},
        {"bar", "ba", "b", {{21,4,0},{21,22,0},{20,0,0}}},
        {"dru", "dr", "d", {{26,0,0},{27,28,0},{29,0,0}}},
        {"ass", "as", "i", {{30,0,0},{31,32,0},{33,34,0}}}};
    for (const auto &character : characters) {
        auto image = std::find_if(std::begin(art), std::end(art),
            [&](const auto &entry) { return character.code == entry.code; });
        if (image == std::end(art)) throw std::runtime_error("Unknown MPQ skill class art");
        ClassSkillTree tree{character.code, image->icon, image->background, {}, {}, {}};
        for (int page = 0; page < 3; ++page) {
            std::string title;
            for (const int fragment : image->headings[page]) {
                if (!fragment) break;
                const auto key = "StrSklTree" + std::to_string(fragment);
                const auto line = strings.find(key);
                if (line.empty()) throw std::runtime_error("Missing original skill tree title: " + key);
                if (!title.empty()) title += '\n';
                title += line;
            }
            tree.pageNames[page] = title;
        }
        catalog.classes.push_back(std::move(tree));
    }
    std::map<std::string, size_t, std::less<>> descriptionsByKey;
    for (size_t row = 0; row < descriptions.rows().size(); ++row)
        if (auto key = descriptions.value(row, "skilldesc"); !key.empty())
            if (!descriptionsByKey.emplace(std::string(key), row).second)
                throw std::runtime_error("Duplicate original SkillDesc key");
    std::map<std::string, int, std::less<>> idsByName;
    for (size_t row = 0; row < skills.rows().size(); ++row) {
        auto classCode = skills.value(row, "charclass");
        if (!catalog.tree(classCode)) continue;
        auto id = skills.number(row, "Id");
        auto name = skills.value(row, "skill");
        auto description = descriptionsByKey.find(skills.value(row, "skilldesc"));
        if (!id || *id < 0 || name.empty() || description == descriptionsByKey.end())
            throw std::runtime_error("Incomplete original class skill record");
        const auto source = description->second;
        auto required = [&](const DataTable &table, size_t index, const char *field) {
            auto value = table.number(index, field);
            if (!value) throw std::runtime_error("Missing original skill field: " + std::string(field));
            return *value;
        };
        SkillRecord entry;
        static_cast<SkillMetadata &>(entry) = loadSkillEligibilityMetadata(skills, row);
        entry.name = name;
        auto display = strings.find(descriptions.value(source, "str name"));
        if (!display.empty()) entry.name = display;
        auto detail = strings.find(descriptions.value(source, "str long"));
        if (!detail.empty()) entry.description = skillText(detail);
        entry.shortDescription = skillText(strings.find(descriptions.value(source, "str short")));
        if (name == "Frozen Armor" || name == "Blaze") {
            entry.secondLabel = strings.find("StrSkill15");
            entry.secondsLabel = strings.find("StrSkill16");
            for (int slot = 1; slot <= 6; ++slot) {
                const auto index = std::to_string(slot);
                const int function = descriptions.number(source, "descline" + index).value_or(0);
                if (!function) continue;
                SkillDescriptionLine line;
                line.function = function;
                line.prefix = strings.find(descriptions.value(source, "desctexta" + index));
                line.suffix = strings.find(descriptions.value(source, "desctextb" + index));
                const auto formula = descriptions.value(source, "desccalca" + index);
                if (function == 1) {
                    line.value = SkillDescriptionValue::Mana;
                    line.prefix = strings.find(descriptions.value(source, "str mana"));
                } else if (name == "Blaze" && function == 23 && descriptions.value(source, "descmissile1") == "blaze")
                    line.value = SkillDescriptionValue::FireDuration;
                else if (name == "Blaze" && function == 27 && descriptions.number(source, "descdam") == 9) {
                    line.value = SkillDescriptionValue::AverageFireDamage;
                    line.prefix = std::string(strings.find("StrSkill68")) + std::string(strings.find("StrSkill5"));
                    line.suffix = strings.find("StrSkill34");
                } else if (function == 3 && skills.value(row, "aurastat1") == "skill_armor_percent" &&
                           formula == skills.value(row, "aurastatcalc1"))
                    line.value = SkillDescriptionValue::DefensePercent;
                else if (function == 12 && !formula.empty()) {
                    if (formula == skills.value(row, "auralencalc")) line.value = SkillDescriptionValue::Duration;
                    else if (formula == skills.value(row, "calc1")) line.value = SkillDescriptionValue::FreezeDuration;
                }
                entry.descriptionLines.push_back(std::move(line));
            }
        }
        for (int slot = 1; slot <= 7; ++slot) {
            const auto index = std::to_string(slot);
            const int function = descriptions.number(source, "dsc3line" + index).value_or(0);
            if (!function) continue;
            const auto textA = std::string(strings.find(descriptions.value(source, "dsc3texta" + index)));
            const auto textB = std::string(strings.find(descriptions.value(source, "dsc3textb" + index)));
            const auto formula = descriptions.value(source, "dsc3calca" + index);
            if (function == 40 && formula == "2") {
                entry.bonusHeading = textA;
                if (const auto marker = entry.bonusHeading.find("%s"); marker != std::string::npos)
                    entry.bonusHeading.replace(marker, 2, textB);
            } else {
                std::optional<int> value;
                if (formula.size() == 4 && formula.starts_with("par") && formula[3] >= '1' && formula[3] <= '8')
                    value = skills.number(row, "Param" + std::string(1, formula[3]));
                // The verified percent-per-level program is independent of learned ranks.
                if (function == 63)
                    entry.bonusDescriptions.push_back(textA + ": +" + (value ? std::to_string(*value) : "?") + "% " + textB);
                else if (function == 67 && formula == "(par7 + 12)/25") {
                    const auto frames = skills.number(row, "Param7");
                    entry.bonusDescriptions.push_back(textA + ": +" +
                        (frames ? std::to_string((*frames + 12) / 25) : "?") + " " + textB);
                } else entry.bonusDescriptions.push_back(textA + ": ? " + textB);
            }
        }
        entry.page = required(descriptions, source, "SkillPage");
        entry.row = required(descriptions, source, "SkillRow");
        entry.column = required(descriptions, source, "SkillColumn");
        entry.iconCell = required(descriptions, source, "IconCel");
        entry.listRow = required(descriptions, source, "ListRow");
        entry.listPool = descriptions.number(source, "ListPool").value_or(0);
        entry.requiredLevel = required(skills, row, "reqlevel");
        entry.maximumRank = required(skills, row, "maxlvl");
        if (entry.page < 1 || entry.page > 3 || entry.row < 1 || entry.row > 6 ||
            entry.column < 1 || entry.column > 3 || entry.iconCell < 0 ||
            entry.requiredLevel < 1 || entry.maximumRank < 1)
            throw std::runtime_error("Invalid original class skill layout");
        if (!catalog.skills.emplace(entry.id, std::move(entry)).second ||
            !idsByName.emplace(std::string(name), *id).second)
            throw std::runtime_error("Duplicate original class skill identity");
    }
    // The numbered CharStats skills are common actions, separate from the thirty point-backed nodes.
    std::set<std::string> requestedCommon{"attack"};
    for (const auto &character : characters)
        for (int slot = 1; slot <= 10; ++slot) {
            auto name = characterStats.value(character.sourceRow, "Skill " + std::to_string(slot));
            if (!name.empty()) requestedCommon.insert(normalized(name));
        }
    std::map<std::string, int, std::less<>> commonByName;
    for (size_t row = 0; row < skills.rows().size(); ++row) {
        if (!skills.value(row, "charclass").empty()) continue;
        auto id = skills.number(row, "Id");
        auto name = skills.value(row, "skill");
        if (!requestedCommon.contains(normalized(name))) continue;
        auto description = descriptionsByKey.find(skills.value(row, "skilldesc"));
        if (!id || *id < 0 || name.empty() || description == descriptionsByKey.end()) continue;
        SkillRecord entry;
        static_cast<SkillMetadata &>(entry) = loadSkillEligibilityMetadata(skills, row);
        entry.animationMode = normalized(skills.value(row, "anim"));
        entry.name = name;
        auto display = strings.find(descriptions.value(description->second, "str name"));
        if (!display.empty()) entry.name = display;
        entry.shortDescription = skillText(strings.find(descriptions.value(description->second, "str short")));
        entry.iconCell = descriptions.number(description->second, "IconCel").value_or(-1);
        entry.listRow = descriptions.number(description->second, "ListRow").value_or(-1);
        entry.listPool = descriptions.number(description->second, "ListPool").value_or(0);
        entry.requiredLevel = 1;
        entry.maximumRank = 1;
        if (entry.iconCell < 0 || !catalog.skills.emplace(entry.id, std::move(entry)).second ||
            !commonByName.emplace(normalized(name), *id).second)
            throw std::runtime_error("Invalid original common skill");
    }
    for (size_t index = 0; index < characters.size(); ++index) {
        auto &tree = catalog.classes[index];
        const auto row = characters[index].sourceRow;
        tree.commonSkills = loadInnateSkillIds(skills, characterStats, row);
        for (const int id : tree.commonSkills)
            if (!catalog.find(id)) throw std::runtime_error("Missing original common skill display");
        auto initial = characterStats.value(row, "StartSkill");
        if (!initial.empty()) {
            auto found = std::find_if(catalog.skills.begin(), catalog.skills.end(),
                [&](const auto &pair) { return pair.second.classCode == tree.classCode &&
                    normalized(pair.second.sourceName) == normalized(initial); });
            if (found == catalog.skills.end())
                throw std::runtime_error("Unknown original starter skill: " + std::string(initial));
            tree.starterSkill = found->first;
        }
    }
    for (size_t row = 0; row < skills.rows().size(); ++row) {
        auto id = skills.number(row, "Id");
        if (!id || !catalog.find(*id)) continue;
        auto &entry = catalog.skills.at(*id);
        if (entry.classCode.empty()) continue;
        for (const int required : entry.prerequisites)
            if (!catalog.find(required) || catalog.skills.at(required).classCode != entry.classCode)
                throw std::runtime_error("Invalid original skill prerequisite for " + entry.sourceName);
    }
    for (const auto &tree : catalog.classes) {
        int count = 0;
        std::array<bool, 54> occupied{};
        for (const auto &[id, entry] : catalog.skills) {
            if (entry.classCode != tree.classCode) continue;
            ++count;
            const int slot = (entry.page - 1) * 18 + (entry.row - 1) * 3 + entry.column - 1;
            if (occupied[slot]) throw std::runtime_error("Duplicate original skill tree cell");
            occupied[slot] = true;
        }
        if (count != 30) throw std::runtime_error("MPQ skill tree lacks thirty class skills");
    }
    return catalog;
}
} // namespace d2x
