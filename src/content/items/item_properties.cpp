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
#include <cctype>
#include "core/random.hpp"

namespace d2x {
namespace {
bool samePropertyCode(const ClassicData &data,std::string_view first,std::string_view second) {
    if(!data.pd2Host) return first==second;
    return first.size()==second.size() && std::equal(first.begin(),first.end(),second.begin(),[](unsigned char left,unsigned char right) {
        return std::tolower(left)==std::tolower(right);
    });
}
}
void loadPropertyData(ClassicData &data) {
    if (data.profile != "lod-named-txt-v1" && data.profile != "remote-display-txt-v1" && !data.pd2Host)
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
                if (!function || !*function)
                    break;
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
                              [&](const auto &property) { return samePropertyCode(data,property.code,code); });
    if (found == data.properties.end() || found->operations.empty()) return false;
    return propertyUsesDirectRoll(found->operations.front().function);
}
SpecialPropertyRoll rollItemProperties(const ClassicData &data,std::span<const PropertyRange> properties,uint64_t seed,int itemLevel) {
    SpecialPropertyRoll result;
    result.randomState=seed;
    for(const auto &property:properties) {
        const auto definition=std::find_if(data.properties.begin(),data.properties.end(),[&](const auto &entry){return samePropertyCode(data,entry.code,property.code);});
        if(definition==data.properties.end()) throw std::runtime_error("Unknown generated property: "+property.code);
        std::vector<int32_t> values;
        int first=0;
        const auto range=[&] {
            const int low=std::min(property.minimum.value_or(0),property.maximum.value_or(0));
            const int high=std::max(property.minimum.value_or(0),property.maximum.value_or(0));
            const auto width=int64_t(high)-low+1;
            if(width>INT32_MAX) throw std::runtime_error("Generated property range exceeds native bounds");
            return low+(low==high?0:int(limitedRandom(result.randomState,uint32_t(width))));
        };
        for(const auto &operation:definition->operations) {
            int value=0,returned=0;
            switch(operation.function) {
            case 1: case 2: case 12: case 13: case 21: case 22: case 36:
                value=range();returned=value;break;
            case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 24:
                value=first?first:range();returned=value;break;
            case 14: value=first>0?first:range();returned=value;break;
            case 15: value=property.minimum.value_or(0);returned=value;break;
            case 16: value=property.maximum.value_or(0);returned=value;break;
            case 17: {
                const auto [end,error]=std::from_chars(property.parameter.data(),property.parameter.data()+property.parameter.size(),value);
                if(error!=std::errc{} || end!=property.parameter.data()+property.parameter.size()) value=0;
                if(!value) value=range();
                returned=value;break;
            }
            case 11: value=property.minimum.value_or(0);returned=value>0?value:5;break;
            case 19: {
                const auto stats=resolvePropertyStats(data,property,0,itemLevel,itemLevel);
                const auto charges=std::find_if(stats.begin(),stats.end(),[&](const auto &entry){return entry.name==operation.stat;});
                if(charges==stats.end()) throw std::runtime_error("Missing generated charged skill");
                const int maximum=(charges->rawValue>>8)&255;
                value=int(limitedRandom(result.randomState,unsigned(maximum-maximum/8)))+maximum/8+1;
                returned=maximum;break;
            }
            case 20: case 23: value=returned=1;break;
            default: throw std::runtime_error("Unsupported generated property function: "+std::to_string(operation.function));
            }
            if(operation.function==12) {
                const auto [end,error]=std::from_chars(property.parameter.data(),property.parameter.data()+property.parameter.size(),returned);
                if(error!=std::errc{} || end!=property.parameter.data()+property.parameter.size()) returned=0;
            } else if(operation.function==36) {
                const auto [end,error]=std::from_chars(operation.value.data(),operation.value.data()+operation.value.size(),returned);
                if(error!=std::errc{} || end!=operation.value.data()+operation.value.size()) returned=0;
            }
            if(values.empty()) first=returned;
            values.push_back(value);
        }
        result.values.push_back(values.empty()?property.minimum.value_or(0):values.front());
        result.operations.push_back(std::move(values));
    }
    return result;
}
std::vector<ResolvedItemStat> resolvePropertyStats(const ClassicData &data,
    const PropertyRange &property, int roll, int level, int itemLevel,std::span<const int32_t> operations) {
    std::vector<ResolvedItemStat> result;
    const auto found = std::find_if(data.properties.begin(), data.properties.end(),
        [&](const auto &p) { return samePropertyCode(data,p.code,property.code); });
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
    input.operationRolls=operations;
    if(!operations.empty() && operations.size()!=found->operations.size()) throw std::runtime_error("Item property operation roll count mismatch");
    std::vector<PropertyInstruction> program;
    program.reserve(found->operations.size());
    for (const auto &op : found->operations) {
        program.push_back({op.function,op.stat,number(op.value),number(op.set)!=0});
        if ((op.function == 11 || op.function == 19) && !input.skill) {
            const auto *skill = data.skills.find(parameter);
            if (skill) input.skill=PropertySkillLevels{skill->requiredLevel,skill->maximumRank};
            else if(data.pd2Host) {
                // Item procs such as proc_SplashDamage are not selectable
                // player skills. Preserve their real table identity and rank
                // metadata without inventing an executable combat program.
                const auto &skills=data.tables.at("skills");
                for(size_t row=0;row<skills.rows().size();++row) if(skills.number(row,"Id")==parameter) {
                    input.skill=PropertySkillLevels{skills.number(row,"reqlevel").value_or(0),skills.number(row,"maxlvl").value_or(0)};
                    break;
                }
            }
            if (!input.skill) throw std::runtime_error("Unknown item property skill: " + property.parameter);
        }
    }
    for (const auto &stat : evaluatePropertyProgram(program,input)) {
        const auto definition = std::find_if(data.itemStats.begin(), data.itemStats.end(),
            [&](const auto &entry) { return entry.name == stat.name; });
        if (definition != data.itemStats.end()) {
            if(stat.set) std::erase_if(result,[&](const auto &entry){return entry.name==stat.name && entry.layer==stat.layer;});
            auto resolved=evaluateItemStat(*definition,stat.value,stat.layer,level);
            resolved.replace=stat.set;
            result.push_back(std::move(resolved));
        }
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
    auto append = [&](const auto &properties, const auto &rolls,const auto &operations) {
        if (properties.size() != rolls.size()) throw std::runtime_error("Item property roll count mismatch");
        if(!operations.empty() && operations.size()!=properties.size()) throw std::runtime_error("Item property program count mismatch");
        for (size_t i = 0; i < properties.size(); ++i) {
            auto stats = resolvePropertyStats(data, properties[i], rolls[i], level, int(item.level),operations.empty()?std::span<const int32_t>{}:operations[i]);
            const auto *base=data.items.find(item.definition);
            const auto program=std::find_if(data.properties.begin(),data.properties.end(),[&](const auto &entry){return samePropertyCode(data,entry.code,properties[i].code);});
            if(base && base->equipment.isType("weap") && program!=data.properties.end() &&
                std::any_of(program->operations.begin(),program->operations.end(),[](const auto &operation){return operation.function==7;})) {
                const int maximum=std::max(base->base.maxDamage.value_or(0),base->base.twoHandMax.value_or(0));
                const auto operation=std::find_if(program->operations.begin(),program->operations.end(),[](const auto &entry){return entry.function==7;});
                const auto index=size_t(operation-program->operations.begin());
                const int percent=operations.empty()?rolls[i]:operations[i][index];
                if(int64_t(maximum)*percent/100<=0) {
                    std::erase_if(stats,[](const auto &entry){return entry.name=="item_mindamage_percent" || entry.name=="item_maxdamage_percent";});
                    const auto maximumStat=std::find_if(data.itemStats.begin(),data.itemStats.end(),[](const auto &entry){return entry.name=="maxdamage";});
                    if(maximumStat==data.itemStats.end()) throw std::runtime_error("Missing maximum damage stat");
                    stats.push_back(evaluateItemStat(*maximumStat,1,0,level));
                }
            }
            for(const auto &stat:stats) if(stat.replace)
                std::erase_if(result,[&](const auto &entry){return entry.name==stat.name && entry.layer==stat.layer;});
            result.insert(result.end(), stats.begin(), stats.end());
        }
    };
    if (item.specialRow >= 0) {
        const auto &records = item.quality == ItemQuality::Unique ? data.uniqueItems : data.setItems;
        for (const auto &r : records)
            if (int32_t(r.row) == item.specialRow) { append(r.properties, item.propertyRolls,item.operationRolls); break; }
    }
    std::vector<const ItemAffixInstance*> ordered;
    if(item.quality==ItemQuality::Rare || item.quality==ItemQuality::Crafted) {
        for(int slot=0;slot<3;++slot) for(bool prefix:{true,false}) {
            int current=0;
            for(const auto &affix:item.affixes) if(affix.prefix==prefix && current++==slot) {ordered.push_back(&affix);break;}
        }
    } else for(const auto &affix:item.affixes) ordered.push_back(&affix);
    for (const auto *selected : ordered) {
        const auto &affix=*selected;
        const auto &records = affix.prefix ? data.magicPrefixes : data.magicSuffixes;
        for (const auto &r : records)
            if (int32_t(r.row) == affix.row) { append(r.properties, affix.propertyRolls,affix.operationRolls); break; }
    }
    if (item.quality == ItemQuality::Superior)
        for (const auto &r : data.superiorGrades)
            if (int32_t(r.row) == item.gradeRow) { append(r.properties, item.propertyRolls,item.operationRolls); break; }
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
