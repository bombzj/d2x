#include "item_properties.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/items/stat_evaluation.hpp"
#include "gameplay/items/property_program.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "socket_data.hpp"
#include <algorithm>
#include <stdexcept>
#include <charconv>
#include <limits>

namespace d2x {
void loadPropertyData(ClassicData &data) {
    if (data.profile != "lod-named-txt-v1")
        return;
    auto properties = data.tables.find("properties");
    if (properties != data.tables.end()) {
        const auto &table = properties->second;
        for (auto column : {"code", "func1", "stat1", "set1", "val1"})
            if (!table.has(column))
                throw std::runtime_error("Properties lacks field: " + std::string(column));
        for (size_t row = 0; row < table.rows().size(); ++row) {
            auto code = table.value(row, "code");
            if (code.empty())
                continue;
            PropertyDefinition definition;
            definition.row = row;
            definition.code = code;
            for (int slot = 1; slot <= 7; ++slot) {
                auto suffix = std::to_string(slot);
                auto function = table.number(row, "func" + suffix);
                if (!function)
                    continue;
                if (*function < 0 || *function > 255)
                    throw std::runtime_error("Invalid original property function: " + definition.code);
                definition.operations.push_back({*function, std::string(table.value(row, "stat" + suffix)),
                                                 std::string(table.value(row, "set" + suffix)),
                                                 std::string(table.value(row, "val" + suffix))});
            }
            data.properties.push_back(std::move(definition));
        }
    }
    auto stats = data.tables.find("itemstatcost");
    if (stats != data.tables.end()) {
        const auto &table = stats->second;
        if (!table.has("Stat") || !table.has("ID"))
            throw std::runtime_error("ItemStatCost lacks Stat or ID");
        for (size_t row = 0; row < table.rows().size(); ++row) {
            auto name = table.value(row, "Stat");
            if (!name.empty()) {
                ItemStatDefinition stat{};
                stat.row = row;
                stat.name = name;
                stat.id = table.number(row, "ID");
                stat.descriptionPriority = table.number(row, "descpriority").value_or(0);
                stat.descriptionFunction = table.number(row, "descfunc").value_or(0);
                stat.descriptionValue = table.number(row, "descval").value_or(0);
                stat.positive = table.value(row, "descstrpos");
                stat.negative = table.value(row, "descstrneg");
                stat.suffix = table.value(row, "descstr2");
                stat.operation = table.number(row, "op").value_or(0);
                stat.operationParameter = table.number(row, "op param").value_or(0);
                stat.operationBase = table.value(row, "op base");
                stat.operationStat = table.value(row, "op stat1");
                for(size_t slot=0;slot<stat.operationStats.size();++slot)
                    stat.operationStats[slot]=table.value(row,"op stat"+std::to_string(slot+1));
                stat.hasMinimum=table.number(row,"fMin").value_or(0)!=0;
                stat.keepZero=table.number(row,"keepzero").value_or(0)!=0;
                stat.damageRelated=table.number(row,"damagerelated").value_or(0)!=0;
                stat.minimumAccrual=table.number(row,"MinAccr").value_or(0);
                stat.valueShift = table.number(row,"ValShift").value_or(0);
                if(stat.valueShift<0 || stat.valueShift>31) throw std::runtime_error("Invalid native stat value shift");
                data.itemStats.push_back(std::move(stat));
            }
        }
        for(auto &stat:data.itemStats) if(!stat.operationStat.empty()) {
            const auto target=std::find_if(data.itemStats.begin(),data.itemStats.end(),[&](const auto &entry){return entry.name==stat.operationStat;});
            if(target==data.itemStats.end()) throw std::runtime_error("Missing native op target stat");
            stat.operationTargetShift=target->valueShift;
        }
    }
}
std::shared_ptr<const StatRuleCatalog> prepareNativeStatRules(const ClassicData &data) {
    auto rules=std::make_shared<StatRuleCatalog>();
    std::map<uint16_t,const ItemStatDefinition*> originals;
    for(const auto &definition:data.itemStats) {
        if(!definition.id || *definition.id<0 || *definition.id>=512) throw std::runtime_error("Invalid native stat identity");
        const auto id=uint16_t(*definition.id);
        if(!rules->names.emplace(definition.name,id).second || !originals.emplace(id,&definition).second)
            throw std::runtime_error("Duplicate native stat definition");
        rules->definitions.emplace(id,StatRule{id,definition.name,uint8_t(definition.valueShift),definition.minimumAccrual,
            definition.hasMinimum,definition.keepZero,definition.damageRelated,{}});
    }
    if(rules->definitions.empty()) throw std::runtime_error("Missing native stat rule catalog");
    const auto &events=data.tables.at("events"),&costs=data.tables.at("itemstatcost");
    for(const auto &[id,definition]:originals) {
        auto &rule=rules->definitions.at(id);
        rule.direct=costs.number(definition->row,"direct").value_or(0)!=0;
        const auto maximum=costs.value(definition->row,"maxstat");
        if(maximum.find_first_not_of(" \t\r\n")!=std::string_view::npos) {
            const auto found=rules->names.find(maximum);
            if(found==rules->names.end()) throw std::runtime_error("Missing native maximum stat link: "+std::string(maximum));
            rule.maximumStat=found->second;
        }
    }
    std::map<std::string,int16_t,std::less<>> eventIds;
    for(size_t row=0;row<events.rows().size();++row) {
        const auto name=events.value(row,"event");
        if(name.empty() || row>INT16_MAX || !eventIds.emplace(std::string(name),int16_t(row)).second)
            throw std::runtime_error("Invalid native unit event linker identity");
    }
    for(const auto &[id,definition]:originals) for(size_t slot=0;slot<2;++slot) {
        const auto suffix=std::to_string(slot+1);
        const auto name=costs.value(definition->row,"itemevent"+suffix);
        auto &binding=rules->definitions.at(id).itemEvents[slot];
        if(name.find_first_not_of(" \t\r\n")!=std::string_view::npos) {
            const auto event=eventIds.find(name);
            if(event==eventIds.end()) throw std::runtime_error("Missing native item event link: "+std::string(name));
            binding.event=event->second;
        }
        const int function=costs.number(definition->row,"itemeventfunc"+suffix).value_or(0);
        if(function<INT16_MIN || function>INT16_MAX) throw std::runtime_error("Invalid native item event function");
        binding.function=int16_t(function);
    }
    // ItemsTbls appends reverse operations by original stat id, not by caller
    // contribution order. This preserves the pre-op percentage reference.
    for(const auto &[id,definition]:originals) {
        if(definition->operation<=0 || definition->operation>=14) continue;
        if(definition->operationParameter<0 || definition->operationParameter>=32)
            throw std::runtime_error("Invalid native stat operation parameter");
        std::optional<uint16_t> base;
        if(!definition->operationBase.empty()) {
            const auto found=rules->names.find(definition->operationBase);
            if(found==rules->names.end()) throw std::runtime_error("Missing native operation base stat");
            base=found->second;
        }
        auto &sourceRule=rules->definitions.at(id);
        sourceRule.producer=StatOperation{uint8_t(definition->operation),uint8_t(definition->operationParameter),id,base};
        if(base) {
            auto &dependents=rules->definitions.at(*base).baseDependents;
            if(dependents.size()>=64) throw std::runtime_error("Native operation-base table exceeds capacity");
            dependents.push_back(id);
        }
        for(const auto &name:definition->operationStats) {
            if(name.empty()) break;
            const auto target=rules->names.find(name);
            if(target==rules->names.end()) throw std::runtime_error("Missing native operation target stat");
            sourceRule.targets.push_back(target->second);
            auto &operations=rules->definitions.at(target->second).operations;
            if(operations.size()>=16) throw std::runtime_error("Native reverse operation table exceeds capacity");
            operations.push_back({uint8_t(definition->operation),uint8_t(definition->operationParameter),id,base});
        }
    }
    return rules;
}
bool isDirectPropertyRoll(const ClassicData &data, std::string_view code) {
    auto found = std::find_if(data.properties.begin(), data.properties.end(),
                              [&](const auto &property) { return property.code == code; });
    if (found == data.properties.end() || found->operations.empty()) return false;
    return propertyUsesDirectRoll(found->operations.front().function);
}
std::vector<ResolvedItemStat> resolvePropertyStats(const ClassicData &data,
    const PropertyRange &property, int roll, int level, int itemLevel) {
    std::vector<ResolvedItemStat> result;
    const auto found = std::find_if(data.properties.begin(), data.properties.end(),
        [&](const auto &p) { return p.code == property.code; });
    if (found == data.properties.end()) return result;
    auto number = [](std::string_view text) {
        int value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size() ? value : 0;
    };
    int parameter = number(property.parameter);
    if (!property.parameter.empty() && !parameter)
        if (auto skills = data.tables.find("skills"); skills != data.tables.end())
            for (size_t row = 0; row < skills->second.rows().size(); ++row)
                if (skills->second.value(row, "skill") == property.parameter)
                    parameter = skills->second.number(row, "Id").value_or(0);
    PropertyProgramInput input{roll,parameter,property.minimum.value_or(0),property.maximum.value_or(0),level,itemLevel,{}};
    std::vector<PropertyInstruction> program;
    program.reserve(found->operations.size());
    for (const auto &op : found->operations) {
        program.push_back({op.function,op.stat,number(op.value)});
        if ((op.function == 11 || op.function == 19) && !input.skill) {
            const auto *skill = data.skills.find(parameter);
            if (!skill) throw std::runtime_error("Unknown item property skill: " + property.parameter);
            input.skill=PropertySkillLevels{skill->requiredLevel,skill->maximumRank};
        }
    }
    for (const auto &stat : evaluatePropertyProgram(program,input)) {
        const auto definition = std::find_if(data.itemStats.begin(), data.itemStats.end(),
            [&](const auto &entry) { return entry.name == stat.name; });
        if (definition != data.itemStats.end()) result.push_back(evaluateItemStat(*definition,stat.value,stat.layer,level));
    }
    return result;
}
std::vector<ResolvedItemStat> resolveOwnItemStats(const ClassicData &data,
    const ItemInstance &item, int level) {
    std::vector<ResolvedItemStat> result;
    if (!item.identified) return result;
    if (item.nativeProperties) {
        for (const auto &saved : item.savedStats) {
            auto definition = std::find_if(data.itemStats.begin(), data.itemStats.end(),
                [&](const auto &entry) { return entry.id == saved.id; });
            if (definition == data.itemStats.end()) throw std::runtime_error("Unknown saved item stat");
            result.push_back(evaluateItemStat(*definition,saved.value,saved.parameter,level));
        }
        return result;
    }
    auto append = [&](const auto &properties, const auto &rolls) {
        if (properties.size() != rolls.size()) throw std::runtime_error("Item property roll count mismatch");
        for (size_t i = 0; i < properties.size(); ++i) {
            auto stats = resolvePropertyStats(data, properties[i], rolls[i], level, int(item.level));
            result.insert(result.end(), stats.begin(), stats.end());
        }
    };
    if (item.specialRow >= 0) {
        const auto &records = item.quality == ItemQuality::Unique ? data.uniqueItems : data.setItems;
        for (const auto &r : records)
            if (int32_t(r.row) == item.specialRow) { append(r.properties, item.propertyRolls); break; }
    }
    for (const auto &affix : item.affixes) {
        const auto &records = affix.prefix ? data.magicPrefixes : data.magicSuffixes;
        for (const auto &r : records)
            if (int32_t(r.row) == affix.row) { append(r.properties, affix.propertyRolls); break; }
    }
    if (item.quality == ItemQuality::Superior)
        for (const auto &r : data.superiorGrades)
            if (int32_t(r.row) == item.gradeRow) { append(r.properties, item.propertyRolls); break; }
    return result;
}
std::vector<ResolvedItemStat> resolveItemStats(const ClassicData &data, const ItemInstance &item, int level) {
    auto result = resolveOwnItemStats(data, item, level);
    auto sockets = resolveSocketStats(data, item, level);
    result.insert(result.end(), sockets.begin(), sockets.end());
    return result;
}
unsigned itemMaximumDurability(const ClassicData &data, const ItemInstance &item, std::span<const ResolvedItemStat> stats) {
    const auto *definition = data.items.find(item.definition);
    if (!definition) return 0;
    const int64_t base = item.nativeProperties ? item.nativeMaxDurability :
        item.quality == ItemQuality::Inferior && definition->maxDurability ? std::max(1u, definition->maxDurability / 3) : definition->maxDurability;
    if (!base) return 0;
    int64_t fixed = 0, percent = 0;
    for (const auto &stat : stats) {
        if (stat.layer) continue;
        if (stat.name == "maxdurability") fixed += stat.rawValue;
        else if (stat.name == "item_maxdurability_percent") percent += stat.rawValue;
    }
    return unsigned(std::clamp(base + fixed + base * percent / 100, int64_t(0), int64_t(std::numeric_limits<int>::max())));
}
} // namespace d2x
