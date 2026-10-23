#include "skill_data.hpp"
#include "skill_eligibility.hpp"
#include "gameplay/skills/necro_passive_spec.hpp"
#include "resources/formats.hpp"
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
void validateSkillDisplayTables(const DataTable &skills, const DataTable &descriptions,
                               std::span<const uint8_t> skillRecords, std::span<const uint8_t> descriptionRecords) {
    // CN retains D2SkillsTxt 0x23C and D2SkillDescTxt 0x120. This validates
    // identities/layout/requirements and display scalars, not equivalence of
    // compiled expressions.
    constexpr size_t skillSize = 0x23C, descriptionSize = 0x120;
    Reader skillBin(skillRecords), descriptionBin(descriptionRecords);
    if (skillBin.u32() != skills.rows().size() || descriptionBin.u32() != descriptions.rows().size() ||
        skillRecords.size() != 4 + skills.rows().size()*skillSize ||
        descriptionRecords.size() != 4 + descriptions.rows().size()*descriptionSize)
        throw std::runtime_error("Skill display BIN/TXT layout mismatch");
    std::map<std::string,size_t,std::less<>> descriptionRows;
    constexpr std::array fields{"SkillPage","SkillRow","SkillColumn","ListRow","ListPool","IconCel"};
    for (size_t row = 0; row < descriptions.rows().size(); ++row) {
        if (!descriptionRows.emplace(normalized(descriptions.value(row,"skilldesc")),row).second)
            throw std::runtime_error("Duplicate SkillDesc linker identity");
        descriptionBin.seek(4 + row*descriptionSize);
        if (descriptionBin.u16() != row) throw std::runtime_error("SkillDesc BIN/TXT identity mismatch");
        for (const auto field : fields) {
            const int value = descriptions.number(row,field).value_or(0);
            if (value < -1 || value > 255 || descriptionBin.u8() != uint8_t(value))
                throw std::runtime_error("SkillDesc BIN/TXT field mismatch: " + std::string(field));
        }
    }
    std::map<std::string,int,std::less<>> skillIds;
    for (size_t row = 0; row < skills.rows().size(); ++row)
        if (const auto id = skills.number(row,"Id"); id && *id >= 0)
            skillIds.emplace(normalized(skills.value(row,"skill")),*id);
    for (size_t row = 0; row < skills.rows().size(); ++row) {
        if (skills.value(row,"charclass").empty()) continue;
        const size_t start = 4 + row*skillSize;
        skillBin.seek(start);
        if (skills.number(row,"Id") != skillBin.u16())
            throw std::runtime_error("Class skill BIN/TXT identity mismatch");
        skillBin.seek(start + 0x194);
        const auto description = descriptionRows.find(normalized(skills.value(row,"skilldesc")));
        if (description == descriptionRows.end() || description->second != skillBin.u16())
            throw std::runtime_error("Class skill BIN/TXT description link mismatch");
        skillBin.seek(start + 0x17E);
        for (const auto field : {"reqskill1","reqskill2","reqskill3"}) {
            int expected = -1;
            if (const auto name = skills.value(row,field); !name.empty()) {
                const auto prerequisite = skillIds.find(normalized(name));
                if (prerequisite == skillIds.end())
                    throw std::runtime_error("Unknown class skill prerequisite: " + std::string(name));
                expected = prerequisite->second;
            }
            if (skillBin.i16() != expected)
                throw std::runtime_error("Class skill BIN/TXT prerequisite mismatch: " + std::string(field));
        }
        for (const auto &[field, offset] : {std::pair{"maxlvl",0x12C}, {"reqlevel",0x174},
             {"reqstr",0x176}, {"reqdex",0x178}, {"reqint",0x17A}, {"reqvit",0x17C},
             {"cltstfunc",0xF2}, {"cltdofunc",0xF4}, {"minmana",0x186}}) {
            skillBin.seek(start + size_t(offset));
            if (skills.number(row,field).value_or(0) != skillBin.u16())
                throw std::runtime_error("Class skill BIN/TXT field mismatch: " + std::string(field));
        }
        for (const auto &[field, offset] : {std::pair{"startmana",0x184}, {"mana",0x18A}, {"lvlmana",0x18C}}) {
            skillBin.seek(start + size_t(offset));
            if (skills.number(row,field).value_or(0) != skillBin.i16())
                throw std::runtime_error("Class skill BIN/TXT field mismatch: " + std::string(field));
        }
        for (const auto &[field, offset] : {std::pair{"manashift",0x188}, {"HitShift",0x1A4}}) {
            skillBin.seek(start + size_t(offset));
            if (skills.number(row,field).value_or(0) != skillBin.u8())
                throw std::runtime_error("Class skill BIN/TXT field mismatch: " + std::string(field));
        }
        for (const auto &[field, offset] : {std::pair{"EMin",0x1E0}, {"EMax",0x1E4}}) {
            skillBin.seek(start + size_t(offset));
            if (skills.number(row,field).value_or(0) != skillBin.i32())
                throw std::runtime_error("Class skill BIN/TXT field mismatch: " + std::string(field));
        }
    }
}
SkillCatalog loadSkillCatalog(const DataTable &skills, const DataTable &descriptions,
                              const DataTable &characterStats,
                              const std::vector<CharacterDefinition> &characters,
                              const ClassicStrings &strings, ContentPreparation preparation) {
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
            if (!descriptionsByKey.emplace(normalized(key), row).second)
                throw std::runtime_error("Duplicate original SkillDesc key");
    std::map<std::string, int, std::less<>> idsByName;
    for (size_t row = 0; row < skills.rows().size(); ++row) {
        auto classCode = skills.value(row, "charclass");
        if (!catalog.tree(classCode)) continue;
        auto id = skills.number(row, "Id");
        auto name = skills.value(row, "skill");
        auto description = descriptionsByKey.find(normalized(skills.value(row, "skilldesc")));
        // A missing description cannot supply a visible node or its icon/layout.
        if (preparation == ContentPreparation::RemoteDisplay && description == descriptionsByKey.end()) continue;
        if (!id || *id < 0 || name.empty() || description == descriptionsByKey.end())
            throw std::runtime_error("Incomplete original class skill record");
        const auto source = description->second;
        // Keep real hidden skills for server/item grants and descriptions. Empty
        // placeholder rows have no ranks and must not become player choices.
        if (preparation == ContentPreparation::RemoteDisplay && skills.number(row, "maxlvl").value_or(0) < 1) continue;
        auto required = [&](const DataTable &table, size_t index, const char *field) {
            auto value = table.number(index, field);
            if (!value) throw std::runtime_error("Missing original skill field: " + std::string(field));
            return *value;
        };
        SkillRecord entry;
        static_cast<SkillMetadata &>(entry) = loadSkillEligibilityMetadata(skills, row, &descriptions);
        entry.name = name;
        auto display = strings.find(descriptions.value(source, "str name"));
        if (!display.empty()) entry.name = display;
        auto detail = strings.find(descriptions.value(source, "str long"));
        if (!detail.empty()) entry.description = skillText(detail);
        entry.shortDescription = skillText(strings.find(descriptions.value(source, "str short")));
        if (preparation == ContentPreparation::OriginalRules && entry.passive && entry.classCode == "nec") {
            NecromancerPassiveSpec passive;
            bool known = true;
            if (name == "Skeleton Mastery") passive.kind = NecromancerPassiveSpec::Kind::SkeletonMastery;
            else if (name == "Golem Mastery") passive.kind = NecromancerPassiveSpec::Kind::GolemMastery;
            else if (name == "Summon Resist") passive.kind = NecromancerPassiveSpec::Kind::SummonResist;
            else known = false;
            if (known) {
                for (size_t parameter = 0; parameter < passive.parameters.size(); ++parameter)
                    passive.parameters[parameter] = skills.number(row,"Param" + std::to_string(parameter + 1)).value_or(0);
                entry.passiveContribution.necromancer = std::make_shared<const NecromancerPassiveSpec>(std::move(passive));
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
        const bool hidden = preparation == ContentPreparation::RemoteDisplay &&
            entry.page >= 0 && entry.page <= 3 && entry.row >= 0 && entry.row <= 6 &&
            entry.column >= 0 && entry.column <= 3 && (!entry.page || !entry.row || !entry.column);
        if ((!entry.hasTreeNode() && !hidden) || entry.iconCell < 0 ||
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
        const bool remoteItemSkill = preparation == ContentPreparation::RemoteDisplay &&
            skills.number(row, "srvdofunc") == 113 && skills.number(row, "scroll") == 1 &&
            skills.number(row, "InGame") == 1;
        if (!requestedCommon.contains(normalized(name)) && !remoteItemSkill) continue;
        auto description = descriptionsByKey.find(normalized(skills.value(row, "skilldesc")));
        // A missing description cannot supply a visible node or its icon/layout.
        if (preparation == ContentPreparation::RemoteDisplay && description == descriptionsByKey.end()) continue;
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
        // Retired hidden skills can retain placeholder prerequisites. Only
        // visible learnable nodes require their prerequisites in the catalog.
        if (entry.classCode.empty() || !entry.hasTreeNode()) continue;
        for (const int required : entry.prerequisites)
            if (!catalog.find(required) || catalog.skills.at(required).classCode != entry.classCode)
                throw std::runtime_error("Invalid original skill prerequisite for " + entry.sourceName);
    }
    for (const auto &tree : catalog.classes) {
        int count = 0;
        std::array<int, 3> pageCounts{};
        std::array<bool, 54> occupied{};
        for (const auto &[id, entry] : catalog.skills) {
            if (entry.classCode != tree.classCode || !entry.hasTreeNode()) continue;
            ++count;
            ++pageCounts[size_t(entry.page - 1)];
            const int slot = (entry.page - 1) * 18 + (entry.row - 1) * 3 + entry.column - 1;
            if (occupied[slot]) throw std::runtime_error("Duplicate original skill tree cell");
            occupied[slot] = true;
        }
        if (preparation == ContentPreparation::OriginalRules && count != 30)
            throw std::runtime_error("MPQ skill tree lacks thirty class skills");
        if (std::any_of(pageCounts.begin(),pageCounts.end(),[](int nodes) { return nodes == 0; }))
            throw std::runtime_error("MPQ skill tree has an empty class page: " + tree.classCode);
    }
    return catalog;
}
} // namespace d2x
