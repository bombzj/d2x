#include "quest_data.hpp"
#include "content/npc/npc_dialogue.hpp"
#include "gameplay/quest/catalog.hpp"
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "gameplay/items/definitions.hpp"
#include <stdexcept>
#include <algorithm>
#include <utility>

namespace d2x {
void loadQuestItemCarryRules(std::vector<ItemDefinition> &items) {
    // ItemMode::sub_6FC428F0 native identities. The shared quest tag alone does
    // not exclude distinct ingredients: e.g. all four Khalim parts may coexist.
    constexpr std::array pairs{
        std::pair{"j34", "g34"}, std::pair{"bks", "bkd"}, std::pair{"d33", "g33"},
        std::pair{"hst", "msf"}, std::pair{"hst", "vip"}, std::pair{"qf2", "qf1"},
        std::pair{"qf2", "qhr"}, std::pair{"qf2", "qey"}, std::pair{"qf2", "qbr"}};
    for (const auto &[first, second] : pairs) {
        auto find = [&](std::string_view code) {
            return std::find_if(items.begin(), items.end(),
                [&](const auto &item) { return item.code == code; });
        };
        auto a = find(first), b = find(second);
        if (a == items.end() || b == items.end() || !a->questTag || a->questTag != b->questTag)
            throw std::runtime_error("Missing original quest carry pair: " + std::string(first) + "/" + second);
        a->questCarryConflicts.emplace_back(second);
        b->questCarryConflicts.emplace_back(first);
    }
}
PrisonOfIceContent loadPrisonOfIceContent(const ItemCatalog &items, const std::map<std::string, DataTable, std::less<>> &tables) {
    PrisonOfIceContent result;
    result.potion = loadNativeQuestItem(items, tables, "ice", 32);
    result.scroll = loadNativeQuestItem(items, tables, "tr2", 32);
    const std::map<std::string, std::vector<std::string>, std::less<>> native{
        {"ama", {"am1", "am2", "am3", "am4", "am5"}}, {"sor", {"ob1", "ob2", "ob3", "ob4", "ob5"}},
        {"nec", {"ne1", "ne2", "ne3", "ne4", "ne5"}}, {"pal", {"pa1", "pa2", "pa3", "pa4", "pa5"}},
        {"bar", {"ba1", "ba2", "ba3", "ba4", "ba5"}}, {"dru", {"dr1", "dr2", "dr3", "dr4", "dr5"}},
        {"ass", {"ktr", "wrb", "axf", "ces", "clw", "btl", "skr"}}};
    for (const auto &[character, bases] : native)
        for (const auto &identity : bases) {
            const auto *base = items.find(identity);
            if (!base) throw std::runtime_error("Missing native Anya reward base: " + identity);
            for (size_t tier = 0; tier < 3; ++tier) {
                const auto code = tier == 0 ? base->code : std::string(tables.at(base->base.sourceTable).value(base->base.sourceRow, tier == 1 ? "ubercode" : "ultracode"));
                const auto *item = items.find(code);
                if (!item || !item->artAvailable) throw std::runtime_error("Missing native Anya reward tier: " + code);
                result.rewards[character][tier].push_back(item->code);
            }
        }
    return result;
}
HellforgeContent loadHellforgeContent(const ItemCatalog &items, const std::map<std::string, DataTable, std::less<>> &tables) {
    HellforgeContent result;
    result.hammer = loadNativeQuestItem(items, tables, "hfh", 25);
    // A4Q3_CreateReward contains native identities and uniform selection; Misc supplies each item definition.
    constexpr std::array<std::array<const char *, 7>, 3> gems{{
        {"gpv", "gpr", "gpb", "gpy", "gpg", "gpw", "skz"},
        {"gzv", "glr", "glb", "gly", "glg", "glw", "skl"},
        {"gsv", "gsr", "gsb", "gsy", "gsg", "gsw", "sku"}}};
    for (size_t tier = 0; tier < gems.size(); ++tier)
        for (size_t i = 0; i < gems[tier].size(); ++i) {
            const auto *item = items.find(gems[tier][i]);
            if (!item || !item->artAvailable) throw std::runtime_error("Missing original Hellforge gem");
            result.gems[tier][i] = item->code;
        }
    constexpr std::array firstRune{1, 12, 15};
    for (size_t difficulty = 0; difficulty < firstRune.size(); ++difficulty)
        for (size_t i = 0; i < result.runes[difficulty].size(); ++i) {
            const int number = firstRune[difficulty] + int(i);
            const auto *item = items.find("r" + std::string(number < 10 ? "0" : "") + std::to_string(number));
            if (!item || !item->artAvailable || item->base.type != "rune") throw std::runtime_error("Missing original Hellforge rune");
            result.runes[difficulty][i] = item->code;
        }
    return result;
}
KhalimRecipeContent loadKhalimRecipeContent(const ItemCatalog &items,
    const std::map<std::string, DataTable, std::less<>> &tables) {
    KhalimRecipeContent result;
    const std::array identities{"qey", "qbr", "qhr", "qf1"};
    for (size_t i = 0; i < identities.size(); ++i) result.inputs[i] = loadNativeQuestItem(items, tables, identities[i], 17);
    const auto &recipes = tables.at("cubemain");
    for (size_t row = 0; row < recipes.rows().size(); ++row) {
        if (!recipes.number(row, "enabled").value_or(0)) continue;
        const auto *output = items.find(recipes.value(row, "output"));
        if (!output || tables.at(output->base.sourceTable).number(output->base.sourceRow, "quest") != 17 ||
            output->code == result.inputs[3]) continue;
        if (recipes.number(row, "numinputs") != 4 || !result.output.empty())
            throw std::runtime_error("Unsupported original Khalim recipe");
        std::array<bool, 4> found{};
        for (int column = 1; column <= 4; ++column) {
            const auto input = recipes.value(row, "input " + std::to_string(column));
            const auto it = std::find(result.inputs.begin(), result.inputs.end(), input);
            if (it == result.inputs.end() || found[size_t(it - result.inputs.begin())])
                throw std::runtime_error("Unsupported original Khalim ingredient");
            found[size_t(it - result.inputs.begin())] = true;
        }
        result.output = loadNativeQuestItem(items, tables, output->code, 17);
    }
    if (result.output.empty()) throw std::runtime_error("Missing original Khalim recipe");
    return result;
}
std::string loadNativeQuestItem(const ItemCatalog &items,
    const std::map<std::string, DataTable, std::less<>> &tables, std::string_view identity, int tag) {
    const auto *item = items.find(identity);
    if (!item || !item->artAvailable || tables.at(item->base.sourceTable).number(item->base.sourceRow, "quest") != tag)
        throw std::runtime_error("Missing original quest item: " + std::string(identity));
    return item->code;
}
GoldenBirdContent loadGoldenBirdContent(const ItemCatalog &items,
    const std::map<std::string, DataTable, std::less<>> &tables) {
    GoldenBirdContent result;
    const auto &misc = tables.at("misc");
    // These are native quest item identities in A3Q4/ItemMode, not item parameters.
    for (const auto &[identity, destination] : std::array{
        std::pair{"j34", &result.figurine}, std::pair{"g34", &result.bird}, std::pair{"xyz", &result.potion}}) {
        const auto *item = items.find(identity);
        if (!item || !item->artAvailable || misc.number(item->base.sourceRow, "quest") != 19 ||
            misc.number(item->base.sourceRow, "questdiffcheck") != 1)
            throw std::runtime_error("Missing original Golden Bird quest item: " + std::string(identity));
        *destination = item->code;
    }
    return result;
}
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
