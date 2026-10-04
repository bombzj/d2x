#include "quest_data.hpp"
#include "content/npc/npc_dialogue.hpp"
#include "gameplay/quest/catalog.hpp"
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "gameplay/items/definitions.hpp"
#include <stdexcept>
#include <utility>

namespace d2x {
StaffRecipeContent loadStaffRecipeContent(const ItemCatalog &items,
    const std::map<std::string, DataTable, std::less<>> &tables) {
    StaffRecipeContent result;
    // For A2Q2 the original Weapons/Misc quest tag is the native quest slot.
    // This is not a general mapping for other quests (e.g. the Act I scroll).
    const auto tag = int(questDefinition(QuestId::HoradricStaff).nativeSlot);
    auto belongs = [&](const ItemDefinition *item) {
        return item && tables.at(item->base.sourceTable).number(item->base.sourceRow, "quest") == tag;
    };
    const auto &recipes = tables.at("cubemain");
    for (size_t row = 0; row < recipes.rows().size(); ++row) {
        if (!recipes.number(row, "enabled").value_or(0)) continue;
        const auto *output = items.find(recipes.value(row, "output"));
        if (!belongs(output) || output->base.type != "staf") continue;
        if (recipes.number(row, "numinputs") != 2)
            throw std::runtime_error("Unsupported original Horadric Staff recipe input count");
        std::array<std::string, 2> inputs;
        for (auto column : {"input 1", "input 2"}) {
            const auto *item = items.find(recipes.value(row, column));
            if (!belongs(item) || (item->base.type != "staf" && item->base.type != "amul"))
                throw std::runtime_error("Unsupported original Horadric Staff recipe ingredient");
            auto &code = inputs[item->base.type == "staf" ? 0 : 1];
            if (!code.empty()) throw std::runtime_error("Ambiguous original Horadric Staff ingredient role");
            code = item->code;
        }
        if (inputs[0].empty() || inputs[1].empty() || !result.output.empty())
            throw std::runtime_error("Ambiguous original Horadric Staff recipe");
        result.inputs = std::move(inputs);
        result.output = output->code;
    }
    const auto &misc = tables.at("misc");
    for (size_t row = 0; row < misc.rows().size(); ++row)
        if (misc.number(row, "quest") == tag && misc.value(row, "type") == "ques") {
            const auto code = misc.value(row, "code");
            const auto *item = items.find(code);
            // The cube shares A2Q2's quest tag/type. Its pSpell=7 capability
            // already parsed by the item loader distinguishes it from the scroll.
            if (item && item->opensCube) continue;
            if (code.empty() || !item || !result.scroll.empty())
                throw std::runtime_error("Ambiguous original Horadric Scroll item");
            result.scroll = code;
        }
    if (result.output.empty() || result.scroll.empty())
        throw std::runtime_error("Original Horadric Staff recipe or scroll is missing");
    return result;
}
QuestContentCatalog loadQuestContent(Archives &archives, const NpcDialogues &dialogues,
    const std::map<std::string, std::string, std::less<>> &strings) {
    QuestContentCatalog result;
    for (const auto &definition : questDefinitions) {
        const auto suffix = std::to_string(definition.act + 1) + "q" + std::to_string(definition.nativeQuest);
        const auto titleKey = "qstsa" + suffix;
        const auto title = strings.find(titleKey);
        if (title == strings.end() || title->second.empty())
            throw std::runtime_error("Original quest title is missing: " + titleKey);
        const auto speechKey = "A" + std::to_string(definition.act + 1) + "Q" + std::to_string(definition.nativeQuest);
        bool found = false;
        for (const auto &[group, speeches] : dialogues)
            for (const auto &speech : speeches)
                if (speech.act == definition.act && speech.quest == speechKey) found = true;
        if (!found) throw std::runtime_error("Original quest dialogue is missing: " + speechKey);
        const auto iconPath = "data/global/ui/menu/a" + suffix + ".dc6";
        if (!archives.contains(iconPath))
            throw std::runtime_error("Original quest artwork is missing: " + iconPath);
        result.at(questIndex(definition.id)) = {title->second, speechKey, iconPath};
    }
    return result;
}
} // namespace d2x
