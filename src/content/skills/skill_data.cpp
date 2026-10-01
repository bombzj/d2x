#include "skill_data.hpp"
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
void applyAuraPassives(CharacterModifiers &modifiers, const SkillCatalog &skills,
    const std::map<int, int> &learned, const CombatEffectSet &effects, EffectFrame frame) {
    for (const auto &[id, skill] : skills.skills) {
        const auto rank = learned.find(id);
        if (rank == learned.end() || effects.hasState(skill.passiveSuppressedByState, frame)) continue;
        modifiers.combat.attackRatingPercent += rank->second * skill.passiveAttackRatingPerBaseRank;
        const int maximum = rank->second / 2;
        if (skill.passiveMaxResistElement == 2) modifiers.combat.fireMaxResist += maximum;
        else if (skill.passiveMaxResistElement == 3) modifiers.combat.lightningMaxResist += maximum;
        else if (skill.passiveMaxResistElement == 4) modifiers.combat.coldMaxResist += maximum;
    }
}
SkillCatalog loadSkillCatalog(const DataTable &skills, const DataTable &descriptions,
                              const DataTable &characterStats,
                              const std::vector<CharacterDefinition> &characters,
                              const ClassicStrings &strings) {
    SkillCatalog catalog;
    constexpr struct { const char *code, *icon, *background; } art[] = {
        {"ama", "am", "a"}, {"sor", "so", "s"}, {"nec", "ne", "n"},
        {"pal", "pa", "p"}, {"bar", "ba", "b"}, {"dru", "dr", "d"},
        {"ass", "as", "i"}};
    for (const auto &character : characters) {
        auto image = std::find_if(std::begin(art), std::end(art),
            [&](const auto &entry) { return character.code == entry.code; });
        if (image == std::end(art)) throw std::runtime_error("Unknown MPQ skill class art");
        ClassSkillTree tree{character.code, image->icon, image->background, {}, {}, {}};
        for (int page = 0; page < 3; ++page) {
            const auto key = characterStats.value(character.sourceRow, "StrSkillTab" + std::to_string(page + 1));
            auto title = strings.find(key);
            if (key.empty() || title.empty())
                throw std::runtime_error("Missing original skill tree title: " + std::string(key));
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
        entry.id = *id;
        entry.classCode = classCode;
        entry.sourceName = name;
        entry.name = name;
        auto display = strings.find(descriptions.value(source, "str name"));
        if (!display.empty()) entry.name = display;
        auto detail = strings.find(descriptions.value(source, "str long"));
        if (!detail.empty()) entry.description = detail;
        entry.page = required(descriptions, source, "SkillPage");
        entry.row = required(descriptions, source, "SkillRow");
        entry.column = required(descriptions, source, "SkillColumn");
        entry.iconCell = required(descriptions, source, "IconCel");
        entry.requiredLevel = required(skills, row, "reqlevel");
        entry.maximumRank = required(skills, row, "maxlvl");
        entry.leftAllowed = skills.number(row, "leftskill").value_or(0) != 0;
        entry.passive = skills.number(row, "passive").value_or(0) != 0;
        entry.allowedInTown = skills.number(row, "InTown").value_or(0) != 0;
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
        entry.id = *id;
        entry.sourceName = name;
        if (name == "Attack") entry.basicAction = BasicSkillAction::Attack;
        else if (name == "Throw") entry.basicAction = BasicSkillAction::Throw;
        else if (name == "Left Hand Swing") entry.basicAction = BasicSkillAction::LeftHandSwing;
        else if (name == "Left Hand Throw") entry.basicAction = BasicSkillAction::LeftHandThrow;
        entry.animationMode = normalized(skills.value(row, "anim"));
        entry.name = name;
        auto display = strings.find(descriptions.value(description->second, "str name"));
        if (!display.empty()) entry.name = display;
        entry.iconCell = descriptions.number(description->second, "IconCel").value_or(-1);
        entry.requiredLevel = 1;
        entry.maximumRank = 1;
        entry.leftAllowed = skills.number(row, "leftskill").value_or(0) != 0;
        entry.allowedInTown = skills.number(row, "InTown").value_or(0) != 0;
        if (entry.iconCell < 0 || !catalog.skills.emplace(entry.id, std::move(entry)).second ||
            !commonByName.emplace(normalized(name), *id).second)
            throw std::runtime_error("Invalid original common skill");
    }
    for (size_t index = 0; index < characters.size(); ++index) {
        auto &tree = catalog.classes[index];
        const auto row = characters[index].sourceRow;
        for (int slot = 1; slot <= 10; ++slot) {
            auto name = characterStats.value(row, "Skill " + std::to_string(slot));
            if (name.empty()) continue;
            auto found = commonByName.find(normalized(name));
            if (found == commonByName.end())
                throw std::runtime_error("Unknown original common skill: " + std::string(name));
            tree.commonSkills.push_back(found->second);
        }
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
        for (auto field : {"reqskill1", "reqskill2", "reqskill3"}) {
            auto key = skills.value(row, field);
            if (key.empty()) continue;
            auto required = idsByName.find(key);
            if (required == idsByName.end() || catalog.skills.at(required->second).classCode != entry.classCode)
                throw std::runtime_error("Invalid original skill prerequisite: " + std::string(key));
            entry.prerequisites.push_back(required->second);
        }
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
