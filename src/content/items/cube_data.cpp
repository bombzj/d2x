#include "cube_data.hpp"
#include "item_properties.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <stdexcept>

namespace d2x {
namespace {
int integer(std::string_view value) {
    int result = 0;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) throw std::runtime_error("Invalid cube integer");
    return result;
}
std::vector<std::string> tokens(std::string_view source) {
    if (source.size() >= 2 && source.front() == '"' && source.back() == '"') source = source.substr(1, source.size() - 2);
    std::vector<std::string> result;
    while (!source.empty()) {
        const auto comma = source.find(','); result.emplace_back(source.substr(0, comma));
        if (comma == std::string_view::npos) break;
        source.remove_prefix(comma + 1);
    }
    return result;
}
std::optional<ItemQuality> quality(const std::string &token) {
    if (token == "low") return ItemQuality::Inferior;
    if (token == "nor") return ItemQuality::Normal;
    if (token == "hiq") return ItemQuality::Superior;
    if (token == "mag") return ItemQuality::Magic;
    if (token == "rar") return ItemQuality::Rare;
    if (token == "set") return ItemQuality::Set;
    if (token == "uni") return ItemQuality::Unique;
    if (token == "crf") return ItemQuality::Crafted;
    return {};
}
bool type(const ClassicData &data, const std::string &code) {
    const auto &table = data.tables.at("itemtypes");
    for (size_t row = 0; row < table.rows().size(); ++row) if (table.value(row, "Code") == code) return true;
    return false;
}
std::vector<ItemInstance::SavedStat> encode(const ClassicData &data, const std::vector<ResolvedItemStat> &stats) {
    std::map<std::pair<int,int>,int> sums;
    for (const auto &stat : stats) {
        if (stat.name == "item_numsockets") continue;
        const auto found = std::find_if(data.itemStats.begin(), data.itemStats.end(),
            [&](const auto &record) { return record.name == stat.name; });
        if (found == data.itemStats.end() || !found->id) throw std::runtime_error("Unknown cube stat: " + stat.name);
        sums[{*found->id, stat.layer}] += stat.rawValue;
    }
    std::vector<ItemInstance::SavedStat> result;
    for (const auto &[key, value] : sums) result.push_back({key.first, key.second, value});
    return result;
}
}
void loadCubeData(ClassicData &data) {
    for (const auto &[code, item] : data.items.entries()) {
        const auto &table = data.tables.at(item.base.sourceTable);
        CubeBaseRecord base;
        base.normal = table.value(item.base.sourceRow, "normcode");
        base.exceptional = table.value(item.base.sourceRow, "ubercode");
        base.elite = table.value(item.base.sourceRow, "ultracode");
        base.minimumStack = unsigned(std::max(0, table.number(item.base.sourceRow, "minstack").value_or(0)));
        const int spawn = table.number(item.base.sourceRow, "spawnstack").value_or(0);
        base.spawnStack = unsigned(spawn < int(base.minimumStack) || !spawn || !item.equipment.quiver.empty() ?
            item.maxStack : std::min(spawn, int(item.maxStack)));
        data.cubeBases.emplace(code, std::move(base));
    }
    const auto &table = data.tables.at("cubemain");
    for (const auto *key : {"enabled", "ladder", "version", "class", "min diff", "op", "numinputs", "input 1", "output"})
        if (!table.has(key)) throw std::runtime_error("Cubemain lacks original field: " + std::string(key));
    for (size_t row = 0; row < table.rows().size(); ++row) {
        if (!table.number(row, "enabled").value_or(0)) continue;
        CubeRecipe recipe; recipe.row = row;
        recipe.version = table.number(row, "version").value_or(0);
        recipe.ladder = table.number(row, "ladder").value_or(0) != 0;
        recipe.minimumDifficulty = table.number(row, "min diff").value_or(0);
        recipe.characterClass = table.value(row, "class");
        recipe.operation = table.number(row, "op").value_or(0);
        if (recipe.operation != 0 && recipe.operation != 28) throw std::runtime_error("Unsupported original cube operation");
        recipe.inputCount = unsigned(table.number(row, "numinputs").value_or(0));
        unsigned count = 0;
        for (int slot = 1; slot <= 7; ++slot) {
            auto values = tokens(table.value(row, "input " + std::to_string(slot)));
            if (values.empty()) continue;
            CubeInput input; input.code = values[0]; input.any = input.code == "any";
            input.type = !input.any && type(data, input.code); // Native input parser checks types before bases.
            if (!input.any && !input.type && !data.items.find(input.code)) {
                bool found = false;
                for (bool unique : {true, false}) {
                    const auto &special = data.tables.at(unique ? "uniqueitems" : "setitems");
                    for (size_t index = 0; index < special.rows().size(); ++index)
                        if (special.value(index, "index") == input.code) {
                            input.code = special.value(index, unique ? "code" : "item");
                            input.specialRow = int(index); input.quality = unique ? ItemQuality::Unique : ItemQuality::Set;
                            found = true; break;
                        }
                    if (found) break;
                }
                if (!found) throw std::runtime_error("Unknown cube input: " + values[0]);
            }
            for (size_t index = 1; index < values.size(); ++index) {
                const auto &token = values[index];
                if (auto q = quality(token)) input.quality = q;
                else if (token.starts_with("qty=")) input.quantity = unsigned(integer(std::string_view(token).substr(4)));
                else if (token == "upg") input.upgraded = true;
                else if (token == "nos") input.noSockets = true;
                else if (token == "sock") input.sockets = true;
                else if (token == "noe") input.noEthereal = true;
                else if (token == "eth") input.ethereal = true;
                else if (token == "nru") input.noRuneword = true;
                else if (token == "bas") input.tier = 1;
                else if (token == "exc") input.tier = 2;
                else if (token == "eli") input.tier = 3;
                else throw std::runtime_error("Unknown cube input qualifier: " + token);
            }
            if (!input.quantity || input.quantity > 48) throw std::runtime_error("Invalid cube quantity");
            count += input.quantity; recipe.inputs.push_back(std::move(input));
        }
        if (!count || count != recipe.inputCount) throw std::runtime_error("Cube original input count mismatch");
        for (const std::string section : {"", " b", " c"}) {
            const std::string fields = section.empty() ? "" : section.substr(1) + " ";
            auto values = tokens(table.value(row, "output" + section));
            if (values.empty()) continue;
            CubeOutput output; output.code = values[0];
            if (output.code == "useitem") output.kind = CubeOutputKind::UseItem;
            else if (output.code == "usetype") output.kind = CubeOutputKind::UseType;
            else if (output.code == "Cow Portal") output.kind = CubeOutputKind::CowPortal;
            else if (output.code == "Pandemonium Portal") output.kind = CubeOutputKind::UberPortal;
            else if (output.code == "Pandemonium Finale Portal") output.kind = CubeOutputKind::UberFinale;
            else if (!data.items.find(output.code)) {
                if (!type(data, output.code)) throw std::runtime_error("Unknown cube output: " + output.code);
                output.kind = CubeOutputKind::Type;
            }
            for (size_t index = 1; index < values.size(); ++index) {
                const auto &token = values[index];
                if (auto q = quality(token)) output.quality = q;
                else if (token == "mod") output.copy = true;
                else if (token == "uns") output.unsocket = true;
                else if (token == "rep") output.repair = true;
                else if (token == "rch") output.recharge = true;
                else if (token == "eth") output.ethereal = true;
                else if (token == "exc") output.tier = 2;
                else if (token == "eli") output.tier = 3;
                else if (token.starts_with("qty=")) output.quantity = unsigned(integer(std::string_view(token).substr(4)));
                else if (token.starts_with("sock=")) output.socketCount = integer(std::string_view(token).substr(5));
                else if (token.starts_with("pre=")) output.prefix = integer(std::string_view(token).substr(4));
                else if (token.starts_with("suf=")) output.suffix = integer(std::string_view(token).substr(4));
                else throw std::runtime_error("Unknown cube output qualifier: " + token);
            }
            output.level = table.number(row, fields + "lvl").value_or(0);
            output.playerPercent = table.number(row, fields + "plvl").value_or(0);
            output.itemPercent = table.number(row, fields + "ilvl").value_or(0);
            for (int slot = 1; slot <= 5; ++slot) {
                const auto prefix = fields + "mod " + std::to_string(slot);
                auto code = table.value(row, prefix);
                if (code.empty()) continue;
                auto found = std::find_if(data.properties.begin(), data.properties.end(), [&](const auto &property) { return property.code == code; });
                if (found == data.properties.end()) throw std::runtime_error("Unknown cube property: " + std::string(code));
                for (const auto &operation : found->operations)
                    if (operation.function != 23 && (operation.function < 1 || operation.function > 24 ||
                        operation.function == 12 || operation.function == 13 || operation.function == 18))
                        throw std::runtime_error("Unsupported cube property function");
                output.properties.push_back({{std::string(code), std::string(table.value(row, prefix + " param")),
                    table.number(row, prefix + " min"), table.number(row, prefix + " max"),
                    isDirectPropertyRoll(data, code)}, table.number(row, prefix + " chance").value_or(0)});
            }
            recipe.outputs.push_back(std::move(output));
        }
        if (recipe.outputs.empty()) throw std::runtime_error("Cube recipe has no original output");
        data.cubeRecipes.push_back(std::move(recipe));
    }
}
void freezeCubeItem(const ClassicData &data, ItemInstance &item, uint64_t *generationRandom) {
    if (item.nativeProperties) return;
    const auto stats = resolveOwnItemStats(data, item, int(item.level));
    auto base = data.items.find(item.definition)->maxDurability;
    if (item.quality == ItemQuality::Inferior && base) base = std::max(1u, base / 3);
    const bool indestructible=std::any_of(stats.begin(),stats.end(),[](const auto &s){return s.name=="item_indesctructible" && s.value;});
    if(base && !indestructible && (item.nativeFlags&0x400000u)) base=base/2+1;
    item.nativeMaxDurability=base; // Original STAT_MAXDURABILITY base, before property modifiers.
    item.savedStats = encode(data, stats);
    if (item.grantedSkill >= 0) {
        auto existing = std::find_if(item.savedStats.begin(), item.savedStats.end(), [&](const auto &stat) {
            return stat.id == 107 && stat.parameter == item.grantedSkill;
        });
        if (existing == item.savedStats.end()) item.savedStats.push_back({107, item.grantedSkill, 1});
        else ++existing->value;
        item.grantedSkill = -1;
    }
    if (item.quality == ItemQuality::Set)
        for (const auto &record : data.setItems) {
            if (int(record.row) != item.specialRow) continue;
            for (const auto &bonus : record.setBonuses) {
                if (!bonus.perItem || record.setAddFunction != 2) continue;
                if (bonus.pieces < 2 || bonus.pieces > 6) throw std::runtime_error("Invalid original set bonus tier");
                int value=bonus.property.minimum.value_or(0);
                if(bonus.property.directRoll && bonus.property.minimum!=bonus.property.maximum) {
                    if(!generationRandom || !bonus.property.minimum || !bonus.property.maximum) throw std::runtime_error("Original variable set bonus has no preserved roll");
                    const int low=std::min(*bonus.property.minimum,*bonus.property.maximum),high=std::max(*bonus.property.minimum,*bonus.property.maximum);
                    value=low+int(limitedRandom(*generationRandom,unsigned(high-low+1)));
                }
                auto &list = item.savedSetStats[size_t(bonus.pieces - 2)];
                for (const auto &stat : encode(data, resolvePropertyStats(data, bonus.property,
                    value, int(item.level), int(item.level)))) {
                    auto existing = std::find_if(list.begin(), list.end(), [&](const auto &entry) {
                        return entry.id == stat.id && entry.parameter == stat.parameter;
                    });
                    if (existing == list.end()) list.push_back(stat);
                    else existing->value += stat.value;
                }
            }
        }
    item.nativeProperties = true;
    item.propertyRolls.clear(); for (auto &affix : item.affixes) affix.propertyRolls.clear();
    item.nativeFlags |= 0x800000u;
}
void updateCubeRequiredLevel(const ClassicData &data, ItemInstance &item) {
    const auto *base = data.items.find(item.definition);
    int level = base->base.requiredLevel.value_or(0);
    for (const auto &affix : item.affixes)
        for (const auto &record : affix.prefix ? data.magicPrefixes : data.magicSuffixes)
            if (int(record.row) == affix.row) level = std::max(level, record.requiredLevel);
    if(item.nativeAutoAffix) for(const auto &record:data.autoMagic)
        if(record.row+1==item.nativeAutoAffix) level=std::max(level,record.requiredLevel);
    if (item.specialRow >= 0)
        for (const auto &record : item.quality == ItemQuality::Unique ? data.uniqueItems : data.setItems)
            if (int(record.row) == item.specialRow) level = std::max(level, record.requiredLevel);
    if (item.quality == ItemQuality::Crafted) level = std::min(98, level + 10 + 3 * int(item.affixes.size()));
    auto identified = item; identified.identified = true;
    for (const auto &stat : resolveOwnItemStats(data, identified, int(item.level)))
        if (stat.name == "item_levelreq") level += stat.rawValue;
    item.requiredLevel = std::clamp(level, 0, 99);
}
void applyCubeProperties(const ClassicData &data, const CubeOutput &output, ItemInstance &item, uint64_t &random) {
    freezeCubeItem(data, item);
    std::map<std::pair<int,int>,int> sums;
    for (const auto &stat : item.savedStats) sums[{stat.id, stat.parameter}] += stat.value;
    for (const auto &entry : output.properties) {
        if (entry.chance > 0 && entry.chance < 100 && int(limitedRandom(random, 100)) > entry.chance) continue;
        const auto &p = entry.property;
        int value = p.minimum.value_or(0);
        if (p.directRoll && p.minimum && p.maximum && p.minimum != p.maximum) {
            const int low = std::min(*p.minimum, *p.maximum), high = std::max(*p.minimum, *p.maximum);
            value = low + int(limitedRandom(random, unsigned(high - low + 1)));
        }
        if (p.code == "sock") {
            const auto &base = *data.items.find(item.definition);
            const int limit = std::max(0, std::min({base.base.sockets.value_or(0),
                base.base.socketsByLevel[item.level <= 25 ? 0 : item.level <= 40 ? 1 : 2], base.width * base.height, 6}));
            if (base.maxStack == 1) item.sockets = unsigned(std::clamp(value, 0, limit));
            if (item.sockets) item.nativeFlags |= 0x800u;
        } else {
            if (p.code == "ac%" && value > 0 && data.items.find(item.definition)->family == ItemFamily::Armor)
                item.defense = data.items.find(item.definition)->base.maxDefense.value_or(item.defense) + 1;
            for (const auto &stat : encode(data, resolvePropertyStats(data, p, value, int(item.level), int(item.level))))
                sums[{stat.id, stat.parameter}] += stat.value;
        }
    }
    item.savedStats.clear();
    for (const auto &[key, value] : sums) item.savedStats.push_back({key.first, key.second, value});
    updateCubeRequiredLevel(data, item);
}
} // namespace d2x
