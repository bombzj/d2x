#include "skill_eligibility.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <stdexcept>

namespace d2x {
std::vector<int> loadInnateSkillIds(const DataTable &skills, const DataTable &characterStats, size_t row) {
    const auto normalized = [](std::string_view value) {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char ch) { return char(std::tolower(ch)); });
        return result;
    };
    std::vector<std::string> names{"attack"};
    std::set<std::string> seen{"attack"};
    for (int slot = 1; slot <= 10; ++slot) {
        const auto name = characterStats.value(row, "Skill " + std::to_string(slot));
        if (!name.empty()) {auto value=normalized(name);if(seen.insert(value).second) names.push_back(std::move(value));}
    }
    std::vector<int> result;
    for (const auto &name : names) {
        bool found = false;
        for (size_t skill = 0; skill < skills.rows().size(); ++skill) {
            if (normalized(skills.value(skill, "skill")) != name) continue;
            const auto id = skills.number(skill, "Id");
            if (id && *id >= 0) { result.push_back(*id); found = true; }
            break;
        }
        if (!found) throw std::runtime_error("Unknown original common skill: " + name);
    }
    return result;
}
SkillMetadata loadSkillEligibilityMetadata(const DataTable &table, size_t row, const DataTable *descriptions) {
    SkillMetadata result;
    result.id = table.number(row, "Id").value_or(-1);
    result.classCode = table.value(row, "charclass");
    result.sourceName = table.value(row, "skill");
    if (descriptions) {
        const auto key = table.value(row, "skilldesc");
        for (size_t description = 0; description < descriptions->rows().size(); ++description) {
            const auto candidate = descriptions->value(description, "skilldesc");
            if (key.empty() || candidate.size() != key.size() || !std::equal(candidate.begin(), candidate.end(), key.begin(),
                [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); })) continue;
            result.page = descriptions->number(description, "SkillPage").value_or(0);
            result.row = descriptions->number(description, "SkillRow").value_or(0);
            result.column = descriptions->number(description, "SkillColumn").value_or(0);
            result.iconCell = descriptions->number(description, "IconCel").value_or(-1);
            result.listRow = descriptions->number(description, "ListRow").value_or(-1);
            result.listPool = descriptions->number(description, "ListPool").value_or(0);
            break;
        }
    }
    result.requiredLevel = table.number(row, "reqlevel").value_or(0);
    result.maximumRank = table.number(row, "maxlvl").value_or(0);
    constexpr std::array fields{"reqstr", "reqdex", "reqvit", "reqint"};
    for (size_t i = 0; i < fields.size(); ++i)
        result.requiredAttributes[i] = table.number(row, fields[i]).value_or(0);
    result.leftAllowed = table.number(row, "leftskill").value_or(0) != 0;
    result.passive = table.number(row, "passive").value_or(0) != 0;
    result.allowedInTown = table.number(row, "InTown").value_or(0) != 0;
    result.inGame = table.number(row, "InGame").value_or(0) != 0;
    result.aura = table.number(row, "aura").value_or(0) != 0;
    result.noAmmo = table.number(row, "noammo").value_or(0) != 0;
    result.startMana = table.number(row, "startmana").value_or(0);
    result.serverDoFunction = table.number(row, "srvdofunc").value_or(0);
    for (size_t hand = 0; hand < 2; ++hand) {
        const auto group = hand ? "b" : "a";
        for (size_t slot = 0; slot < 3; ++slot)
            result.includedHandTypes[hand][slot] = table.value(row, "itype" + std::string(group) + std::to_string(slot + 1));
        for (size_t slot = 0; slot < 2; ++slot)
            result.excludedHandTypes[hand][slot] = table.value(row, "etype" + std::string(group) + std::to_string(slot + 1));
    }
    // Non-item monster rows contain whitespace in this numeric flag.
    // Only SrvDo113 interprets scroll/book quantity; do not parse other rows.
    result.itemSkill = table.number(row, "srvdofunc") == 113 &&
        table.number(row, "scroll").value_or(0) != 0;
    if (result.sourceName == "Attack") result.basicAction = BasicSkillAction::Attack;
    else if (result.sourceName == "Throw") result.basicAction = BasicSkillAction::Throw;
    else if (result.sourceName == "Left Hand Swing") result.basicAction = BasicSkillAction::LeftHandSwing;
    else if (result.sourceName == "Left Hand Throw") result.basicAction = BasicSkillAction::LeftHandThrow;
    for (const char *field : {"reqskill1", "reqskill2", "reqskill3"}) {
        const auto name = table.value(row, field);
        if (name.empty()) continue;
        bool found = false;
        for (size_t prerequisite = 0; prerequisite < table.rows().size(); ++prerequisite) {
            const auto candidate = table.value(prerequisite, "skill");
            // TXT linkers resolve ASCII identifiers without regard to case.
            if (candidate.size() != name.size() || !std::equal(candidate.begin(), candidate.end(), name.begin(),
                [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); })) continue;
            const auto id = table.number(prerequisite, "Id");
            if (id && *id >= 0) { result.prerequisites.push_back(*id); found = true; }
            break;
        }
        // Classless item/monster records can retain retired TXT names. The
        // native linker leaves those at -1; they are not learnable nodes.
        if (!found && !result.classCode.empty())
            throw std::runtime_error("Invalid original skill prerequisite: " + std::string(name));
    }
    return result;
}
SkillEligibility evaluateSkillEligibility(const SkillMetadata &skill, const SkillEligibilityInput &input) {
    SkillEligibility result;
    result.available = input.innate || (input.effectiveRank && *input.effectiveRank > 0);
    // CharStats installs these skills even without a usable scroll/book.
    // D2Common SKILLS_GetUseState returns NOQUANTITY independently of rank.
    if (skill.itemSkill) result.available = input.itemQuantity && *input.itemQuantity > 0;
    result.pickerEnabled = result.available && !skill.passive && !input.dead && input.equipmentReady.value_or(true);
    result.usableNow = result.pickerEnabled && (!input.town || skill.allowedInTown);
    if (input.mana && input.requiredMana) result.usableNow &= *input.mana >= *input.requiredMana;
    result.canAllocate = skill.hasTreeNode() && !skill.classCode.empty() && skill.classCode == input.classCode &&
        input.baseRank && *input.baseRank >= 0 && *input.baseRank < skill.maximumRank &&
        input.skillPoints && *input.skillPoints > 0 && input.level &&
        int64_t(*input.level) >= int64_t(skill.requiredLevel) + *input.baseRank;
    for (size_t i = 0; i < skill.requiredAttributes.size(); ++i)
        if (skill.requiredAttributes[i] > 0)
            result.canAllocate &= input.attributes[i] && *input.attributes[i] >= skill.requiredAttributes[i];
    for (const int id : skill.prerequisites) {
        const auto rank = input.prerequisiteRanks.find(id);
        result.canAllocate &= rank != input.prerequisiteRanks.end() && rank->second > 0;
    }
    return result;
}
} // namespace d2x
