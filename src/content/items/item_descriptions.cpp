#include "item_properties.hpp"
#include <algorithm>
#include <map>

namespace d2x {
std::vector<std::string> describeItemStats(const ClassicData &data, const ItemInstance &item, int level) {
    const auto stats = resolveItemStats(data, item, level);
    std::map<std::pair<std::string, int>, int> sums;
    for (const auto &stat : stats) sums[{stat.name, stat.layer}] += stat.value;
    auto text = [&](std::string_view key) -> std::string {
        auto found = data.itemStrings.find(key);
        return found == data.itemStrings.end() ? std::string{} : found->second;
    };
    auto value = [&](const char *stat) { return sums[{stat, 0}]; };
    std::vector<std::pair<int, std::string>> descriptions;
    auto range = [&](const char *minimum, const char *maximum, const char *key) {
        const int low = value(minimum), high = value(maximum);
        if (!low && !high) return;
        auto description = text(key);
        if (description.empty()) return;
        auto substitute = [&](int number) {
            auto at = description.find("%d");
            if (at != std::string::npos) description.replace(at, 2, std::to_string(number));
        };
        substitute(low); substitute(high);
        descriptions.emplace_back(100, description);
        sums.erase({minimum, 0}); sums.erase({maximum, 0});
    };
    // The paired damage strings are original TBL entries used by the client.
    range("firemindam", "firemaxdam", "strModFireDamageRange");
    range("lightmindam", "lightmaxdam", "strModLightningDamageRange");
    range("coldmindam", "coldmaxdam", "strModColdDamageRange");
    range("magicmindam", "magicmaxdam", "strModMagicDamageRange");
    const int poisonSources = int(std::count_if(stats.begin(), stats.end(),
        [](const auto &stat) { return stat.name == "poisonmaxdam" && stat.value > 0; }));
    if (const int frames = value("poisonlength") / std::max(1, poisonSources); frames > 0) {
        const int minimum = int(int64_t(value("poisonmindam")) * frames / 256);
        const int maximum = int(int64_t(value("poisonmaxdam")) * frames / 256);
        auto description = text(minimum == maximum ? "strModPoisonDamage" : "strModPoisonDamageRange");
        auto substitute = [&](int number) {
            auto at = description.find("%d");
            if (at != std::string::npos) description.replace(at, 2, std::to_string(number));
        };
        substitute(minimum);
        if (minimum != maximum) substitute(maximum);
        substitute(frames / 25);
        if (!description.empty()) descriptions.emplace_back(100, std::move(description));
        sums.erase({"poisonmindam", 0}); sums.erase({"poisonmaxdam", 0});
    }
    if (value("item_mindamage_percent") == value("item_maxdamage_percent"))
        sums.erase({"item_mindamage_percent", 0});
    for (const auto &[key, number] : sums) {
        if (!number) continue;
        auto found = std::find_if(data.itemStats.begin(), data.itemStats.end(),
            [&](const auto &s) { return s.name == key.first; });
        if (found == data.itemStats.end() || !found->descriptionFunction) continue;
        const auto &stat = *found;
        auto description = text(number < 0 ? stat.negative : stat.positive);
        const int function = stat.descriptionFunction;
        int shown = number;
        if (function == 5) shown = number * 100 / 128;
        if (function == 11) shown = std::max(1, 100 / std::max(1, number));
        const bool signedValue = function == 1 || function == 2 || function == 4 ||
                                 function == 6 || function == 7 || function == 8 ||
                                 function == 12 || function == 20 || function == 27 || function == 28;
        std::string amount = (signedValue && shown >= 0 ? "+" : "") + std::to_string(shown);
        if (function == 2 || function == 4 || function == 5 || function == 7 || function == 8 || function == 20)
            amount += "%";
        if (function == 13) {
            // CharStats supplies the localized class skill label for this layer.
            const auto &classes = data.tables.at("charstats");
            if (key.second >= 0 && size_t(key.second) < classes.rows().size()) {
                auto classText = text(classes.value(size_t(key.second), "StrAllSkills"));
                if (!classText.empty()) description = classText;
            }
            amount = "+" + std::to_string(shown);
        } else if (function == 14) {
            const int classIndex = key.second / 8, tab = key.second % 8;
            const auto &classes = data.tables.at("charstats");
            if (classIndex < 0 || size_t(classIndex) >= classes.rows().size() || tab > 2) continue;
            description = text(classes.value(size_t(classIndex), "StrSkillTab" + std::to_string(tab + 1)));
            if (description.empty()) continue;
            auto suffix = text(classes.value(size_t(classIndex), "StrClassOnly"));
            if (!suffix.empty()) description += " " + suffix;
            amount = "+" + std::to_string(shown);
        } else if (function == 27 || function == 28) {
            auto skill = data.skills.skills.find(key.second);
            if (skill == data.skills.skills.end()) continue;
            description = skill->second.name;
            if (function == 27) {
                const auto &classes = data.tables.at("charstats");
                for (const auto &character : data.characters)
                    if (character.code == skill->second.classCode) {
                        auto suffix = text(classes.value(character.sourceRow, "StrClassOnly"));
                        if (!suffix.empty()) description += " " + suffix;
                    }
            }
            descriptions.emplace_back(stat.descriptionPriority, amount + " to " + description);
            continue;
        } else if (function >= 14 && function != 20) {
            continue; // Layered skill triggers/charges have a separate formatter.
        }
        if (description.empty()) continue;
        auto placeholder = description.find("%d");
        if (placeholder != std::string::npos)
            description.replace(placeholder, 2, std::to_string(shown));
        else if (stat.descriptionValue == 1) description = amount + " " + description;
        else if (stat.descriptionValue == 2) description += " " + amount;
        if (!stat.suffix.empty()) {
            auto suffix = text(stat.suffix);
            if (!suffix.empty()) description += " " + suffix;
        }
        descriptions.emplace_back(stat.descriptionPriority, std::move(description));
    }
    std::stable_sort(descriptions.begin(), descriptions.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    std::vector<std::string> result;
    for (auto &[priority, line] : descriptions) result.push_back(std::move(line));
    return result;
}
} // namespace d2x
