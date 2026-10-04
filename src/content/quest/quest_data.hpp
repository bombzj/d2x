#pragma once
#include "gameplay/quest/id.hpp"
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
class Archives;
struct NpcDialogues;
class ItemCatalog;
struct ItemDefinition;
class DataTable;
void loadQuestItemCarryRules(std::vector<ItemDefinition> &items);
struct PrisonOfIceContent {
    std::string potion, scroll;
    std::map<std::string, std::array<std::vector<std::string>, 3>, std::less<>> rewards;
};
PrisonOfIceContent loadPrisonOfIceContent(const ItemCatalog &, const std::map<std::string, DataTable, std::less<>> &);
struct QuestContent {
    std::string title, speechKey, iconPath;
};
using QuestContentCatalog = std::array<QuestContent, size_t(QuestId::Count)>;
struct GoldenBirdContent { std::string figurine, bird, potion; };
struct HellforgeContent {
    std::string hammer;
    std::array<std::array<std::string, 7>, 3> gems;
    std::array<std::array<std::string, 11>, 3> runes;
};
HellforgeContent loadHellforgeContent(const ItemCatalog &, const std::map<std::string, DataTable, std::less<>> &);
struct KhalimRecipeContent {
    std::array<std::string, 4> inputs; // Eye, brain, heart, flail, independent of Cubemain column order.
    std::string output;
    bool isWeapon(std::string_view code) const { return !code.empty() && (code == inputs[3] || code == output); }
};
KhalimRecipeContent loadKhalimRecipeContent(const ItemCatalog &items,
    const std::map<std::string, DataTable, std::less<>> &tables);
std::string loadNativeQuestItem(const ItemCatalog &items,
    const std::map<std::string, DataTable, std::less<>> &tables, std::string_view identity, int tag);
GoldenBirdContent loadGoldenBirdContent(const ItemCatalog &items,
    const std::map<std::string, DataTable, std::less<>> &tables);
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
