#include "socket_data.hpp"
#include "item_properties.hpp"
#include "content/string_table.hpp"
#include "gameplay/items/state.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <stdexcept>

namespace d2x {
namespace {
PropertyRange property(const ClassicData &content, const DataTable &table, size_t row,
    const std::string &code, const std::string &param, const std::string &min, const std::string &max) {
    PropertyRange result{std::string(table.value(row, code)), std::string(table.value(row, param)),
        table.number(row, min), table.number(row, max), isDirectPropertyRoll(content, table.value(row, code))};
    if (!result.code.empty() && std::none_of(content.properties.begin(), content.properties.end(),
        [&](const auto &entry) { return entry.code == result.code; }))
        throw std::runtime_error("Unknown socket property: " + result.code);
    return result;
}
int roll(const PropertyRange &property, uint64_t &random) {
    if (!property.directRoll || !property.minimum || !property.maximum) return property.minimum.value_or(0);
    const int low = std::min(*property.minimum, *property.maximum), high = std::max(*property.minimum, *property.maximum);
    if (low == high) return low;
    return low + int(limitedRandom(random, unsigned(high - low + 1)));
}
std::vector<ItemInstance::SavedStat> saved(const ClassicData &content, const std::vector<ResolvedItemStat> &stats) {
    std::map<std::pair<int, int>, int> sums;
    for (const auto &stat : stats) {
        const auto found = std::find_if(content.itemStats.begin(), content.itemStats.end(),
            [&](const auto &entry) { return entry.name == stat.name; });
        if (found == content.itemStats.end() || !found->id) throw std::runtime_error("Unknown socket stat: " + stat.name);
        sums[{*found->id, stat.layer}] += stat.rawValue;
    }
    std::vector<ItemInstance::SavedStat> result;
    for (const auto &[key, value] : sums) result.push_back({key.first, key.second, value});
    return result;
}
}
void loadSocketData(ClassicData &content, const ClassicStrings &strings) {
    const auto &gems = content.tables.at("gems");
    for (auto key : {"code", "weaponMod1Code", "helmMod1Code", "shieldMod1Code"})
        if (!gems.has(key)) throw std::runtime_error("Gems lacks original field: " + std::string(key));
    for (size_t row = 0; row < gems.rows().size(); ++row) {
        const auto code = gems.value(row, "code");
        if (code.empty()) continue;
        const auto *base = content.items.find(code);
        if (!base || !base->equipment.isType("sock")) throw std::runtime_error("Unknown gem/rune base: " + std::string(code));
        GemRecord record;
        for (int group = 0; group < 3; ++group)
            for (int slot = 1; slot <= 3; ++slot) {
                const auto prefix = std::string(group == 0 ? "weaponMod" : group == 1 ? "helmMod" : "shieldMod") + std::to_string(slot);
                for (auto suffix : {"Code", "Param", "Min", "Max"})
                    if (!gems.has(prefix + suffix)) throw std::runtime_error("Gems lacks " + prefix + suffix);
                auto value = property(content, gems, row, prefix + "Code", prefix + "Param", prefix + "Min", prefix + "Max");
                if (value.directRoll && value.minimum != value.maximum)
                    throw std::runtime_error("Variable gem roll has no compact D2S field: " + std::string(code));
                if (!value.code.empty()) record.properties[size_t(group)].push_back(std::move(value));
            }
        if (!content.socketGems.emplace(std::string(code), std::move(record)).second)
            throw std::runtime_error("Duplicate gem/rune base: " + std::string(code));
    }
    const auto &runes = content.tables.at("runes");
    for (auto key : {"Name", "complete", "server", "Rune1", "itype1", "etype1"})
        if (!runes.has(key)) throw std::runtime_error("Runes lacks original field: " + std::string(key));
    for (size_t row = 0; row < runes.rows().size(); ++row) {
        if (runes.value(row, "Name").empty()) continue;
        RunewordRecord record;
        record.row = int(row); record.complete = runes.number(row, "complete").value_or(0) != 0;
        record.server = runes.number(row, "server").value_or(0) != 0;
        record.stringId = strings.index(runes.value(row, "Name"));
        record.name = strings.find(runes.value(row, "Name"));
        for (int slot = 1; slot <= 6; ++slot) {
            auto rune = runes.value(row, "Rune" + std::to_string(slot));
            if (!rune.empty()) record.runes.emplace_back(rune);
            auto type = runes.value(row, "itype" + std::to_string(slot));
            if (!type.empty()) record.includedTypes.emplace_back(type);
            if (slot <= 3) {
                auto excluded = runes.value(row, "etype" + std::to_string(slot));
                if (!excluded.empty()) record.excludedTypes.emplace_back(excluded);
            }
        }
        if (record.complete) {
            if (record.runes.empty() || record.runes.size() > 6 || record.name.empty() ||
                record.stringId <= 0 || record.stringId >= 65535 || record.includedTypes.empty())
                throw std::runtime_error("Invalid original complete runeword: " + std::string(runes.value(row, "Name")));
            for (const auto &rune : record.runes)
                if (!content.socketGems.contains(rune) || !content.items.find(rune)->equipment.isType("rune"))
                    throw std::runtime_error("Runeword references an unknown rune: " + rune);
            for (int slot = 1; slot <= 7; ++slot) {
                const auto prefix = "T1";
                const auto suffix = std::to_string(slot);
                auto value = property(content, runes, row, prefix + std::string("Code") + suffix,
                    prefix + std::string("Param") + suffix, prefix + std::string("Min") + suffix, prefix + std::string("Max") + suffix);
                if (!value.code.empty()) record.properties.push_back(std::move(value));
            }
        }
        content.runewords.push_back(std::move(record));
    }
    // The narrow socket-removal recipe is prepared from the original row;
    // the general cube interpreter remains a separate item-system task.
    const auto &cube = content.tables.at("cubemain");
    auto cubeText = [&](size_t row, const std::string &key) {
        std::string value(cube.value(row, key));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        return value;
    };
    for (size_t row = 0; row < cube.rows().size(); ++row) {
        const auto output = cubeText(row, "output");
        if (!cube.number(row, "enabled").value_or(0) ||
            (cube.value(row, "mod 1") != "sock" && !output.starts_with("useitem,sock="))) continue;
        if (cube.number(row, "op").value_or(0) || !cube.value(row, "class").empty() ||
            cube.number(row, "ladder").value_or(0) || !cube.value(row, "mod 2").empty() ||
            !cube.value(row, "output b").empty() || !cube.value(row, "output c").empty())
            throw std::runtime_error("Unsupported socket cube recipe qualifiers");
        SocketRecipe recipe;
        recipe.row = int(row); recipe.version = cube.number(row, "version").value_or(0);
        recipe.minimumDifficulty = cube.number(row, "min diff").value_or(0);
        auto target = cubeText(row, "input 1");
        const auto comma = target.find(',');
        recipe.itemType = target.substr(0, comma);
        if (comma != std::string::npos) {
            target.erase(0, comma + 1);
            while (!target.empty()) {
                const auto next = target.find(','); const auto token = target.substr(0, next);
                if (token == "nor") recipe.quality = ItemQuality::Normal;
                else if (token == "mag") recipe.quality = ItemQuality::Magic;
                else if (token == "rar") recipe.quality = ItemQuality::Rare;
                else if (token == "nos") recipe.requiresNoSockets = true;
                else if (token == "sock") recipe.requiresSockets = true;
                else throw std::runtime_error("Unsupported socket cube input qualifier: " + token);
                if (next == std::string::npos) break;
                target.erase(0, next + 1);
            }
        }
        unsigned inputs = 1;
        for (int slot = 2; slot <= 7; ++slot) {
            auto input = cubeText(row, "input " + std::to_string(slot));
            if (input.empty()) continue;
            SocketRecipeMaterial material;
            const auto quantity = input.find(",qty=");
            if (quantity != std::string::npos) {
                auto text = std::string_view(input).substr(quantity + 5);
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), material.quantity);
                if (error != std::errc{} || end != text.data() + text.size() || !material.quantity)
                    throw std::runtime_error("Invalid socket cube material quantity");
                input.resize(quantity);
            }
            if (content.items.find(input)) material.code = input;
            else {
                const auto &types = content.tables.at("itemtypes");
                for (size_t index = 0; index < types.rows().size(); ++index)
                    if (types.value(index, "Code") == input) { material.code = input; material.type = true; break; }
                if (material.code.empty()) {
                    const auto &unique = content.tables.at("uniqueitems");
                    for (size_t index = 0; index < unique.rows().size(); ++index)
                        if (unique.value(index, "index") == input && unique.number(index, "enabled").value_or(0)) {
                            material.code = unique.value(index, "code"); material.uniqueRow = int(index); break;
                        }
                }
            }
            if (material.code.empty()) throw std::runtime_error("Unknown socket cube material: " + input);
            inputs += material.quantity; recipe.materials.push_back(std::move(material));
        }
        if (inputs != unsigned(cube.number(row, "numinputs").value_or(0)))
            throw std::runtime_error("Invalid socket cube input count");
        recipe.rerollMagic = output == "usetype,mag";
        if (output.starts_with("useitem,sock=")) {
            const auto count = std::string_view(output).substr(13);
            const auto [end, error] = std::from_chars(count.data(), count.data() + count.size(), recipe.minimum);
            if (error != std::errc{} || end != count.data() + count.size()) throw std::runtime_error("Invalid cube socket output");
            recipe.maximum = recipe.minimum;
        } else {
            if (output != "useitem" && !recipe.rerollMagic) throw std::runtime_error("Unsupported cube socket output");
            recipe.minimum = cube.number(row, "mod 1 min").value_or(0);
            recipe.maximum = cube.number(row, "mod 1 max").value_or(0);
        }
        if (recipe.minimum < 1 || recipe.maximum < recipe.minimum || recipe.maximum > 6)
            throw std::runtime_error("Invalid cube socket roll range");
        recipe.level = cube.number(row, "lvl").value_or(0);
        recipe.playerLevelPercent = cube.number(row, "plvl").value_or(0);
        recipe.itemLevelPercent = cube.number(row, "ilvl").value_or(0);
        content.socketRecipes.push_back(std::move(recipe));
    }
    for (size_t row = 0; row < cube.rows().size(); ++row) {
        auto output = cube.value(row, "output");
        if (output != "\"useitem,uns\"" && output != "useitem,uns") continue;
        auto text = [&](const char *key) {
            std::string value(cube.value(row, key));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
            return value;
        };
        if (!cube.number(row, "enabled").value_or(0)) continue;
        if (content.unsocketRecipe.enabled || cube.number(row, "numinputs") != 3 ||
            cube.number(row, "op").value_or(0) || !cube.value(row, "class").empty() ||
            cube.number(row, "ladder").value_or(0))
            throw std::runtime_error("Unsupported original unsocket recipe qualifiers");
        const auto target = text("input 1");
        if (!target.ends_with(",sock")) throw std::runtime_error("Unsupported unsocket target");
        auto &recipe = content.unsocketRecipe;
        recipe.enabled = true; recipe.itemType = target.substr(0, target.size() - 5);
        recipe.materials = {text("input 2"), text("input 3")};
        recipe.version = cube.number(row, "version").value_or(0);
        recipe.minimumDifficulty = cube.number(row, "min diff").value_or(0);
        for (const auto &code : recipe.materials)
            if (!content.items.find(code)) throw std::runtime_error("Unknown unsocket material: " + code);
    }
}
const RunewordRecord *matchRuneword(const ClassicData &content, const ItemInstance &item) {
    const auto *base = content.items.find(item.definition);
    if (!base || base->questTag || item.sockets == 0 || item.socketedItems.size() != item.sockets ||
        (item.quality != ItemQuality::Normal && item.quality != ItemQuality::Superior && item.quality != ItemQuality::Inferior))
        return nullptr;
    for (const auto &record : content.runewords) {
        if (!record.complete || record.runes.size() != item.socketedItems.size()) continue;
        bool sequence = true;
        for (size_t index = 0; index < record.runes.size(); ++index)
            sequence &= record.runes[index] == item.socketedItems[index].definition;
        auto type = [&](const std::string &code) { return base->equipment.isType(code); };
        if (sequence && std::none_of(record.excludedTypes.begin(), record.excludedTypes.end(), type) &&
            std::any_of(record.includedTypes.begin(), record.includedTypes.end(), type)) return &record;
    }
    return nullptr;
}
int socketRequiredLevel(const ClassicData &content, const ItemInstance &item) {
    int result = 0;
    for (const auto &child : item.socketedItems) {
        const auto *base = content.items.find(child.definition);
        if (!base) throw std::runtime_error("Unknown socket child base");
        result = std::max({result, base->base.requiredLevel.value_or(0), child.requiredLevel});
    }
    return result;
}
void prepareSocketedItem(const ClassicData &content, ItemInstance &item, uint64_t &random) {
    item.socketRequiredLevel = socketRequiredLevel(content, item);
    const auto *record = matchRuneword(content, item);
    if (!record) { item.runewordRow = -1; item.runewordStats.clear(); item.nativeFlags &= ~0x4000000u; return; }
    if (item.runewordRow == record->row) return;
    std::vector<ResolvedItemStat> stats;
    for (const auto &property : record->properties) {
        auto resolved = resolvePropertyStats(content, property, roll(property, random), int(item.level));
        if (property.code == "ethereal") {
            // PropertyFunc23 sets the native flag and physical base bonus once.
            if (!(item.nativeFlags & 0x400000u)) {
                item.nativeFlags |= 0x400000u;
                if (content.items.find(item.definition)->family == ItemFamily::Armor) item.defense = item.defense * 3 / 2;
            }
        }
        for (auto &stat : resolved) {
            if (stat.name == "item_charged_skill") {
                const unsigned maximum = unsigned(stat.rawValue) >> 8;
                stat.value = stat.rawValue = int((maximum << 8) | (limitedRandom(random, maximum - maximum / 8) + maximum / 8 + 1));
            }
        }
        stats.insert(stats.end(), resolved.begin(), resolved.end());
    }
    item.runewordStats = saved(content, stats); item.runewordRow = record->row;
    item.nativeFlags |= 0x4000000u;
}
std::vector<ResolvedItemStat> resolveSocketStats(const ClassicData &content, const ItemInstance &item, int level) {
    std::vector<ResolvedItemStat> result;
    if (!item.identified) return result;
    const auto *host = content.items.find(item.definition);
    for (const auto &child : item.socketedItems) {
        std::vector<ResolvedItemStat> stats;
        const auto gem = content.socketGems.find(child.definition);
        if (gem != content.socketGems.end()) {
            if (!host || host->gemApplyType < 0 || host->gemApplyType > 2) throw std::runtime_error("Invalid socket host apply type");
            auto random = initialRandom(child.nativeSeed);
            for (const auto &property : gem->second.properties[size_t(host->gemApplyType)]) {
                auto resolved = resolvePropertyStats(content, property, roll(property, random), level);
                stats.insert(stats.end(), resolved.begin(), resolved.end());
            }
        } else stats = resolveOwnItemStats(content, child, level);
        result.insert(result.end(), stats.begin(), stats.end());
    }
    ItemInstance bonus;
    bonus.nativeProperties = true; bonus.savedStats = item.runewordStats;
    auto stats = resolveOwnItemStats(content, bonus, level);
    result.insert(result.end(), stats.begin(), stats.end());
    return result;
}
} // namespace d2x
