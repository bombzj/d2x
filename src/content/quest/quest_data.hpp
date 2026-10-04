#pragma once
#include "gameplay/quest/id.hpp"
#include <array>
#include <map>
#include <string>
#include <string_view>

namespace d2x {
class Archives;
struct NpcDialogues;
class ItemCatalog;
class DataTable;
struct QuestContent {
    std::string title, speechKey, iconPath;
};
using QuestContentCatalog = std::array<QuestContent, size_t(QuestId::Count)>;
struct StaffRecipeContent {
    std::array<std::string, 2> inputs; // Shaft then head, resolved from MPQ item types.
    std::string output, scroll;
    bool isComponent(std::string_view code) const {
        return !code.empty() && (code == output || code == inputs[0] || code == inputs[1]);
    }
};
StaffRecipeContent loadStaffRecipeContent(const ItemCatalog &items,
    const std::map<std::string, DataTable, std::less<>> &tables);
// Native numbering is a rule identity. Text and resource bindings are prepared
// here from original MPQ conventions, never synthesized in gameplay or UI.
QuestContentCatalog loadQuestContent(Archives &archives, const NpcDialogues &dialogues,
    const std::map<std::string, std::string, std::less<>> &strings);
} // namespace d2x
