#include "item_properties.hpp"
#include "item_appearance.hpp"
#include "gameplay/items/state.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace d2x {
namespace {
std::string trimmed(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
// TBL strings are data, never host printf formats. %% consumes no argument.
std::string format(std::string_view pattern, std::initializer_list<std::string> arguments) {
    std::string result;
    auto argument = arguments.begin();
    for (size_t index = 0; index < pattern.size(); ++index) {
        if (pattern[index] != '%' || index + 1 == pattern.size()) { result += pattern[index]; continue; }
        const char code = pattern[index + 1];
        if (code == '%') { result += '%'; ++index; }
        else if ((code == 'd' || code == 's') && argument != arguments.end()) { result += *argument++; ++index; }
        else result += pattern[index];
    }
    return trimmed(std::move(result));
}
} // namespace

std::vector<std::string> describeItemStats(const ClassicData &data, const ItemInstance &item, int level, std::string_view characterClass) {
    return describeItemStats(data, resolveItemStats(data, item, level), characterClass, item.specialRow);
}
std::vector<std::string> describeItemStats(const ClassicData &data, std::span<const ResolvedItemStat> stats, std::string_view characterClass, int itemFileIndex, std::optional<int> ownerMissingHp) {
    using Key = std::pair<std::string, int>;
    std::map<Key, int> sums;
    std::set<Key> present, consumed;
    for (const auto &stat : stats) {
        sums[{stat.name, stat.layer}] += stat.value;
        if (stat.rawValue || stat.value) present.emplace(stat.name, stat.layer);
    }
    auto text = [&](std::string_view key) -> std::string {
        const auto found = data.itemStrings.find(key);
        return found == data.itemStrings.end() ? std::string{} : trimmed(found->second);
    };
    auto definition = [&](std::string_view name) -> const ItemStatDefinition * {
        const auto found = std::find_if(data.itemStats.begin(), data.itemStats.end(), [&](const auto &entry) { return entry.name == name; });
        return found == data.itemStats.end() ? nullptr : &*found;
    };
    auto value = [&](const char *name) { const auto found = sums.find({name, 0}); return found == sums.end() ? 0 : found->second; };
    struct Description { int priority, id; std::string text; };
    std::vector<Description> descriptions;
    auto append = [&](const ItemStatDefinition &stat, std::string description) {
        if (!description.empty()) descriptions.push_back({stat.descriptionPriority, stat.id.value_or(0), std::move(description)});
    };
    auto range = [&](const char *minimum, const char *maximum, const char *singleKey, const char *rangeKey) {
        const int low = value(minimum), high = value(maximum);
        if (!low && !high) return;
        const auto *stat = definition(minimum);
        if (!stat) return;
        const auto description = low < high ? format(text(rangeKey), {std::to_string(low), std::to_string(high)})
            : format(text(singleKey), {std::to_string(std::max(low, high))});
        if (description.empty()) return;
        append(*stat, description);
        consumed.emplace(minimum, 0); consumed.emplace(maximum, 0);
    };
    range("firemindam", "firemaxdam", "strModFireDamage", "strModFireDamageRange");
    range("lightmindam", "lightmaxdam", "strModLightningDamage", "strModLightningDamageRange");
    range("coldmindam", "coldmaxdam", "strModColdDamage", "strModColdDamageRange");
    range("magicmindam", "magicmaxdam", "strModMagicDamage", "strModMagicDamageRange");
    if (value("mindamage") > 0 && value("mindamage") < value("maxdamage"))
        if (const auto *stat = definition("mindamage")) {
            append(*stat, format(text("strModMinDamageRange"), {std::to_string(value("mindamage")), std::to_string(value("maxdamage"))}));
            consumed.emplace("mindamage", 0); consumed.emplace("maxdamage", 0);
        }
    // Native 540F4 reads poison_count (326); decoded items may expose only
    // the separate own/socket lists, whose contributing rates supply the count.
    const int poisonSources = value("poison_count") > 0 ? value("poison_count") :
        int(std::count_if(stats.begin(), stats.end(), [](const auto &stat) { return stat.name == "poisonmaxdam" && stat.value > 0; }));
    if (const int frames = value("poisonlength") / std::max(1, poisonSources); frames > 0)
        if (const auto *stat = definition("poisonmindam")) {
            const int minimum = int((int64_t(value("poisonmindam")) * frames + 128) >> 8);
            const int maximum = int((int64_t(value("poisonmaxdam")) * frames + 128) >> 8);
            append(*stat, minimum < maximum
                ? format(text("strModPoisonDamageRange"), {std::to_string(minimum), std::to_string(maximum), std::to_string(frames / 25)})
                : format(text("strModPoisonDamage"), {std::to_string(std::max(minimum, maximum)), std::to_string(frames / 25)}));
            consumed.emplace("poisonmindam", 0); consumed.emplace("poisonmaxdam", 0);
        }
    if (const int percent = value("item_mindamage_percent"); percent && percent == value("item_maxdamage_percent"))
        if (const auto *stat = definition("item_mindamage_percent")) {
            append(*stat, (percent > 0 ? "+" : "") + std::to_string(percent) + "% " + text("itemEnhancedDamage"));
            consumed.emplace("item_mindamage_percent", 0); consumed.emplace("item_maxdamage_percent", 0);
        }
    const auto &costs = data.tables.at("itemstatcost");
    auto describe = [&](int layer, int number, int function, int placement, std::string_view positive,
                        std::string_view negative, std::string_view suffixKey) -> std::string {
        auto description = text(number < 0 ? negative : positive);
        const auto &classes = data.tables.at("charstats");
        static constexpr std::string_view classCodes[]{"ama", "sor", "nec", "pal", "bar", "dru", "ass"};
        auto classRow = [&](int nativeClass) -> std::optional<size_t> {
            if (nativeClass < 0 || nativeClass >= int(std::size(classCodes))) return {};
            for (const auto &character : data.characters)
                if (character.code == classCodes[nativeClass]) return character.sourceRow;
            return {};
        };
        if (function == 13) {
            const auto row = classRow(layer);
            if (!row) return {};
            return "+" + std::to_string(number) + " " + text(classes.value(*row, "StrAllSkills"));
        }
        if (function == 14) {
            const int classIndex = layer / 8, tab = layer % 8;
            const auto row = classRow(classIndex);
            if (!row || tab < 0 || tab > 2) return {};
            description = format(text(classes.value(*row, "StrSkillTab" + std::to_string(tab + 1))), {std::to_string(number)});
            const auto only = text(classes.value(*row, "StrClassOnly"));
            return description.empty() ? std::string{} : description + (only.empty() ? "" : " " + only);
        }
        if (function == 15 || function == 16 || function == 24 || function == 27 || function == 28) {
            const int shift = costs.number(0, "stuff").value_or(6);
            if (shift <= 0 || shift > 8 || layer < 0) return {};
            const bool packed = function == 15 || function == 24;
            const auto *skill = data.skills.find(packed ? layer >> shift : layer);
            if (!skill) return {};
            if (function == 28 && !characterClass.empty() && skill->classCode == characterClass)
                number = std::min(number, 3); // Native oskill cap for the skill's own class.
            const auto rank = std::to_string(unsigned(layer) & ((1u << shift) - 1));
            if (function == 15) return format(description, {std::to_string(number), rank, skill->name});
            if (function == 16) return format(description, {std::to_string(number), skill->name});
            if (function == 24) {
                const unsigned current = unsigned(number) & 0xff, maximum = unsigned(number) >> 8;
                if (number < 0 || number > 65535 || current > maximum) return {};
                return text("ModStre10b") + " " + rank + " " + skill->name + " " + format(description, {std::to_string(current), std::to_string(maximum)});
            }
            description = "+" + std::to_string(number) + " " + text("to") + " " + skill->name;
            if (function == 27)
                for (const auto &character : data.characters)
                    if (character.code == skill->classCode) {
                        const auto only = text(classes.value(character.sourceRow, "StrClassOnly"));
                        if (!only.empty()) description += " " + only;
                    }
            return description;
        }
        if (function == 11) {
            const int frames = number > 0 ? 2500 / number : 0;
            return frames <= 30 ? format(text("ModStre9t"), {number > 0 ? "1" : "25"})
                : format(text("ModStre9u"), {"1", std::to_string((frames + 12) / 25)});
        }
        if (description.empty()) return {};
        if (function == 19) return format(description, {std::to_string(number)});
        int shown = number;
        if (function == 5 || function == 10) shown = int(int64_t(number) * 100 / 128);
        if (function == 20 || function == 21) shown = -number;
        const bool signedValue = function == 1 || function == 4 || function == 6 || function == 8 || function == 12 || function == 22;
        const bool percent = function == 2 || function == 4 || function == 5 || function == 7 || function == 8 || function == 10 || function == 20 || function == 22 || function == 23;
        if (function < 1 || function > 23 || function == 17 || function == 18) return {};
        std::string amount = (signedValue && shown >= 0 ? "+" : "") + std::to_string(shown) + (percent ? "%" : "");
        if (function == 12 && number == 1) amount.clear();
        if (placement == 1 && !amount.empty()) description = amount + " " + description;
        else if (placement == 2 && !amount.empty()) description += " " + amount;
        if (function == 23) {
            const auto &monsters = data.tables.at("monstats");
            bool found = false;
            for (size_t row = 0; row < monsters.rows().size(); ++row)
                if (data.profile == "remote-display-txt-v1" ? layer >= 0 && row == size_t(layer)
                    : monsters.number(row, "hcIdx") == layer) {
                    const auto name = text(monsters.value(row, "NameStr"));
                    if (name.empty()) return {};
                    description += " " + name; found = true; break;
                }
            if (!found) return {};
        } else if (function == 22) {
            const auto &types = data.tables.at("montype");
            if (layer < 0 || size_t(layer) >= types.rows().size()) return {};
            const auto name = text(types.value(size_t(layer), "strplur"));
            if (name.empty()) return {};
            description += text("colon") + " " + name;
        }
        if ((function >= 6 && function <= 10) || function == 21) {
            const auto suffix = text(suffixKey);
            if (!suffix.empty()) description += " " + suffix;
        }
        return description;
    };
    // Visit ItemStatCost IDs, retaining original order for equal priorities.
    for (const auto &stat : data.itemStats)
        for (const auto &[key, number] : sums) {
            if (key.first != stat.name || !present.contains(key) || consumed.contains(key) || !stat.descriptionFunction) continue;
            if (!number && stat.operationBase != "level") continue;
            if (data.profile == "remote-display-txt-v1" && stat.descriptionFunction == 30) {
                if (stat.id == 270) {
                    append(stat, format(text(pd2ItemEditionText(itemFileIndex)), {std::to_string(number)}));
                }
                continue;
            }
            if (data.profile == "remote-display-txt-v1" && stat.id == 191 && key.second == 605 && number == 1) {
                const auto description = text("StrPhasingStatDesc");
                if (!description.empty()) {
                    append(stat, format(description, {std::to_string(number)}));
                    continue;
                }
            }
            const int group = costs.number(stat.row, "dgrp").value_or(0);
            std::vector<Key> members;
            bool equal = group != 0 && key.second == 0;
            if (equal)
                for (const auto &member : data.itemStats) {
                    if (costs.number(member.row, "dgrp").value_or(0) != group) continue;
                    const Key memberKey{member.name, 0};
                    const auto found = sums.find(memberKey);
                    if (found == sums.end() || found->second != number || consumed.contains(memberKey)) { equal = false; break; }
                    members.push_back(memberKey);
                }
            if (equal && members.size() > 1) {
                auto description = describe(0, number, costs.number(stat.row, "dgrpfunc").value_or(0), costs.number(stat.row, "dgrpval").value_or(0),
                    costs.value(stat.row, "dgrpstrpos"), costs.value(stat.row, "dgrpstrneg"), costs.value(stat.row, "dgrpstr2"));
                if (!description.empty()) {
                    // Native 54AC0 lets only the lowest-ID group member emit;
                    // its own priority (not the highest member priority) applies.
                    descriptions.push_back({stat.descriptionPriority, stat.id.value_or(0), std::move(description)});
                    consumed.insert(members.begin(), members.end());
                    continue;
                }
            }
            int displayNumber = number;
            if (data.profile == "remote-display-txt-v1" && stat.id == 489 && ownerMissingHp) {
                const uint32_t product = uint32_t(number) * uint32_t(*ownerMissingHp);
                displayNumber = std::min(int32_t{40}, std::bit_cast<int32_t>(product));
            }
            if (data.profile == "remote-display-txt-v1" && stat.name == "lifedrain_percentcap") {
                const int64_t complement = int64_t{100} - number;
                if (complement < std::numeric_limits<int>::min() || complement > std::numeric_limits<int>::max()) continue;
                displayNumber = int(complement);
            }
            append(stat, describe(key.second, displayNumber, stat.descriptionFunction, stat.descriptionValue, stat.positive, stat.negative, stat.suffix));
        }
    std::stable_sort(descriptions.begin(), descriptions.end(), [](const auto &left, const auto &right) {
        return left.priority != right.priority ? left.priority > right.priority : left.id < right.id;
    });
    std::vector<std::string> result;
    for (auto &description : descriptions) result.push_back(std::move(description.text));
    return result;
}
} // namespace d2x
