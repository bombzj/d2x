#include "core/random.hpp"
#include "item_magic_loot.hpp"
#include "item_properties.hpp"
#include "item_quality.hpp"
#include "gameplay/loot/affix.hpp"
#include "gameplay/loot/special.hpp"
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace d2x {
AffixGenerationResult generatePropertyItem(const ClassicData &data,const ItemDefinition &item,ItemQuality quality,
    int itemLevel,uint64_t seed,std::span<const ItemPropertyTarget> targets) {
    if(targets.empty() || targets.size()>12 || (quality!=ItemQuality::Magic && quality!=ItemQuality::Rare))
        throw std::invalid_argument("properties requires 1..12 targets and magic or rare quality");
    const auto rules=loadItemQualityRules(data,data.tables.at("itemratio"),item.code);
    if(rules.normalOnly || rules.uniqueOnly || (quality==ItemQuality::Rare && !rules.rareAllowed) || item.questTag)
        throw std::invalid_argument("This base cannot legally have the requested affix quality");
    const int magic=item.base.magicLevel.value_or(0);
    const int level=itemAffixLevel(itemLevel,item.base.level.value_or(0),magic);
    const auto canonicalParameter=[&](std::string_view parameter) {
        if(parameter.empty()) return std::string("0");
        const auto &skills=data.tables.at("skills");
        for(size_t row=0;row<skills.rows().size();++row)
            if(skills.value(row,"skill")==parameter) return std::to_string(skills.number(row,"Id").value());
        return std::string(parameter);
    };
    for(size_t index=0;index<targets.size();++index) {
        const auto &target=targets[index];
        const auto definition=std::find_if(data.properties.begin(),data.properties.end(),[&](const auto &entry){return entry.code==target.code;});
        const std::string path="properties["+std::to_string(index)+"] ("+target.code+"): ";
        if(definition==data.properties.end() || definition->operations.empty()) throw std::invalid_argument(path+"unknown or empty MPQ Properties code");
        if(target.value.has_value()==(target.minimum.has_value() || target.maximum.has_value()) ||
            (!target.value && (!target.minimum || !target.maximum)))
            throw std::invalid_argument(path+"supply value OR both min and max");
        if(target.value && !isDirectPropertyRoll(data,target.code))
            throw std::invalid_argument(path+"this property requires min/max/param, not value");
        if(!target.value && isDirectPropertyRoll(data,target.code))
            throw std::invalid_argument(path+"this property requires an exact value, not a random min/max range");
        for(size_t previous=0;previous<index;++previous)
            if(target.code==targets[previous].code && canonicalParameter(target.parameter)==canonicalParameter(targets[previous].parameter))
                throw std::invalid_argument(path+"duplicate property and parameter; specify its value once");
    }
    struct Candidate {
        bool prefix{};
        const MagicAffixRecord *record{};
        uint32_t covered{};
        std::vector<std::optional<int>> overrides;
    };
    std::vector<Candidate> candidates;
    uint32_t available=0;
    for(bool prefix:{true,false}) {
        const auto &records=prefix?data.magicPrefixes:data.magicSuffixes;
        for(const auto &record:records) {
            const auto eligible=rollMagicAffix(std::span(&record,1),item.equipment.types,item.equipment.requiredClass,
                level,magic,quality==ItemQuality::Rare,item.base.sockets.value_or(0)>0,{},true,seed,int(record.row)+1);
            if(!eligible.row) continue;
            Candidate candidate{prefix,&record,0,std::vector<std::optional<int>>(record.properties.size())};
            bool compatible=true;
            for(size_t slot=0;slot<record.properties.size() && compatible;++slot) {
                const auto &property=record.properties[slot];
                for(size_t index=0;index<targets.size();++index) {
                    const auto &target=targets[index];
                    if(property.code!=target.code || canonicalParameter(property.parameter)!=canonicalParameter(target.parameter)) continue;
                    const auto mask=uint32_t(1)<<index;
                    if(candidate.covered&mask) {compatible=false;break;}
                    if(target.value) {
                        if(!property.directRoll || !property.minimum || !property.maximum ||
                            *target.value<std::min(*property.minimum,*property.maximum) || *target.value>std::max(*property.minimum,*property.maximum)) {compatible=false;break;}
                        candidate.overrides[slot]=*target.value;
                    } else if(property.minimum.value_or(0)!=*target.minimum || property.maximum.value_or(0)!=*target.maximum) {compatible=false;break;}
                    candidate.covered|=mask;
                }
            }
            if(compatible) {available|=candidate.covered;candidates.push_back(std::move(candidate));}
        }
    }
    const uint32_t complete=(uint32_t(1)<<targets.size())-1;
    if(available!=complete) for(size_t index=0;index<targets.size();++index)
        if(!(available&(uint32_t(1)<<index))) throw std::invalid_argument("properties["+std::to_string(index)+"] ("+targets[index].code+"): no legal affix matches this base, level, quality, parameter and value; values are per property, not a sum of affixes");
    std::vector<const Candidate*> selected;
    unsigned work=0;
    bool exhausted=false;
    const unsigned sideLimit=quality==ItemQuality::Magic?1u:3u;
    const unsigned totalLimit=quality==ItemQuality::Magic?2u:item.equipment.isType("jewl")?4u:6u;
    const unsigned minimumCount=quality==ItemQuality::Magic?1u:3u;
    std::function<bool(uint32_t,unsigned,unsigned)> search=[&](uint32_t covered,unsigned prefixes,unsigned suffixes) {
        if(++work>100000) {exhausted=true;return false;}
        if(covered==complete && selected.size()>=minimumCount) return true;
        if(selected.size()>=totalLimit) return false;
        size_t next=0;
        if(covered!=complete) while(covered&(uint32_t(1)<<next)) ++next;
        for(const auto &candidate:candidates) {
            if((covered!=complete && !(candidate.covered&(uint32_t(1)<<next))) || (candidate.covered&covered) ||
                (candidate.prefix?prefixes:suffixes)>=sideLimit ||
                std::any_of(selected.begin(),selected.end(),[&](const auto *entry){return entry->record->group==candidate.record->group;})) continue;
            selected.push_back(&candidate);
            if(search(covered|candidate.covered,prefixes+unsigned(candidate.prefix),suffixes+unsigned(!candidate.prefix))) return true;
            selected.pop_back();
            if(exhausted) return false;
        }
        return false;
    };
    if(!search(0,0,0)) throw std::invalid_argument(exhausted?"Property search budget exceeded; reduce targets or specify affixes explicitly":"Requested properties conflict: no combination satisfies affix groups and prefix/suffix limits");
    auto result=rollAffixItem(data,item,quality,itemLevel,seed,{});
    if(!result.deferred.empty()) throw std::invalid_argument(result.deferred);
    result.generation.affixes.clear();result.generation.requiredLevel=0;
    for(const auto *candidate:selected) {
        const auto &record=*candidate->record;
        auto properties=rollItemProperties(data,record.properties,result.randomState,itemLevel);
        result.randomState=properties.randomState;
        for(size_t slot=0;slot<candidate->overrides.size();++slot) if(candidate->overrides[slot]) {
            properties.values[slot]=*candidate->overrides[slot];
            std::fill(properties.operations[slot].begin(),properties.operations[slot].end(),*candidate->overrides[slot]);
        }
        result.generation.affixes.push_back({candidate->prefix,int32_t(record.row),std::move(properties.values),std::move(properties.operations)});
        result.generation.requiredLevel=std::max(result.generation.requiredLevel,record.requiredLevel);
    }
    result.generation.propertyRandom=result.randomState;
    return result;
}
AffixGenerationResult rollAffixItem(const ClassicData &data, const ItemDefinition &item,
                                    ItemQuality quality, int itemLevel, uint64_t seed,
                                    std::string_view /*characterClass*/, int preferredPrefix, int preferredSuffix) {
    AffixGenerationResult result;
    result.randomState = seed;
    result.generation.quality = quality;
    if (item.equipment.types.empty() ||
        (quality != ItemQuality::Magic && quality != ItemQuality::Rare && quality != ItemQuality::Crafted)) {
        result.deferred = "Unsupported affix item base: " + item.code;
        return result;
    }
    const int magicLevel = item.base.magicLevel.value_or(0);
    const int affixLevel = itemAffixLevel(itemLevel, item.base.level.value_or(0), magicLevel);
    const bool socketable = item.base.sockets.value_or(0) > 0;
    std::vector<int> usedGroups;
    if ((quality == ItemQuality::Rare || quality == ItemQuality::Crafted)) {
        auto chooseName = [&](std::span<const RareNameRecord> records) -> int32_t {
            std::vector<const RareNameRecord *> candidates;
            for (const auto &record : records) {
                auto matches = [&](const std::string &type) { return item.equipment.isType(type); };
                if (std::any_of(record.excludedTypes.begin(), record.excludedTypes.end(), matches) ||
                    !std::any_of(record.includedTypes.begin(), record.includedTypes.end(), matches))
                    continue;
                candidates.push_back(&record);
            }
            if (candidates.empty())
                return -1;
            rollRandom(result.randomState);
            return int32_t(candidates[uint32_t(result.randomState) % candidates.size()]->row);
        };
        result.generation.rarePrefixRow = chooseName(std::span<const RareNameRecord>(data.rarePrefixes));
        result.generation.rareSuffixRow = chooseName(std::span<const RareNameRecord>(data.rareSuffixes));
        if (result.generation.rarePrefixRow < 0 || result.generation.rareSuffixRow < 0) {
            result.deferred = "No eligible original rare name: " + item.code;
            return result;
        }
    }
    auto append = [&](bool prefix, bool force) {
        const auto &records = prefix ? data.magicPrefixes : data.magicSuffixes;
        const int preferred = prefix ? preferredPrefix : preferredSuffix;
        // D2MOO checks the item's class restriction, not the buyer/killer's class.
        auto roll = rollMagicAffix(records, std::span<const std::string>(item.equipment.types), item.equipment.requiredClass,
                                   affixLevel, magicLevel, quality != ItemQuality::Magic, socketable,
                                   std::span<const int>(usedGroups), force,
                                   result.randomState, preferred);
        result.randomState = roll.randomState;
        if (!roll.row) return false;
        auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto &record) { return record.row == *roll.row; });
        if (found == records.end())
            return false;
        std::vector<int32_t> values;
        std::vector<std::vector<int32_t>> operations;
        if (quality == ItemQuality::Magic) {
            auto properties = rollItemProperties(data,found->properties, result.randomState,itemLevel);
            result.randomState = properties.randomState;
            values = std::move(properties.values);
            operations=std::move(properties.operations);
        }
        result.generation.affixes.push_back({prefix, int32_t(found->row), std::move(values),std::move(operations)});
        result.generation.requiredLevel = std::max(result.generation.requiredLevel,
                                                    found->requiredLevel);
        usedGroups.push_back(found->group);
        return true;
    };
    if (quality == ItemQuality::Magic) {
        const bool prefix = preferredPrefix >= 0 && append(true, preferredPrefix > 0);
        if (!prefix || preferredSuffix >= 0)
            append(false, !prefix || preferredSuffix > 0);
    } else {
        auto below = [&](uint32_t limit) {
            rollRandom(result.randomState);
            return uint32_t(result.randomState) % limit;
        };
        int target;
        if (quality == ItemQuality::Crafted) {
            const int minimum = itemLevel > 70 ? 4 : itemLevel > 50 ? 3 : itemLevel > 30 ? 2 : 1;
            target = std::max(minimum, int(below(5)));
        } else {
            constexpr int counts[]{3, 4, 4, 5, 5, 5, 6, 6};
            target = item.equipment.isType("jewl") ? 3 + int(below(2)) : counts[below(8)];
        }
        int prefixes = 0, suffixes = 0;
        bool prefixesExhausted = false, suffixesExhausted = false;
        for (int attempt = 0; attempt < target && (!prefixesExhausted || !suffixesExhausted);) {
            const bool choosePrefix = quality == ItemQuality::Crafted
                ? ((below(2) == 0 || suffixes >= 3) && prefixes < 3)
                : (suffixesExhausted || (!prefixesExhausted && below(2) == 0));
            if (append(choosePrefix, true)) {
                (choosePrefix ? prefixes : suffixes)++;
                ++attempt;
            } else if (quality == ItemQuality::Crafted) {
                ++attempt;
            } else {
                (choosePrefix ? prefixesExhausted : suffixesExhausted) = true;
            }
            prefixesExhausted = prefixesExhausted || prefixes >= 3;
            suffixesExhausted = suffixesExhausted || suffixes >= 3;
        }
        for (int slot = 0; slot < 3; ++slot) {
            for (bool prefix : {true, false}) {
                int current = 0;
                for (auto &affix : result.generation.affixes) {
                    if (affix.prefix != prefix || current++ != slot) continue;
                    const auto &records = prefix ? data.magicPrefixes : data.magicSuffixes;
                    const auto found = std::find_if(records.begin(), records.end(),
                        [&](const auto &record) { return int32_t(record.row) == affix.row; });
                    auto properties = rollItemProperties(data,found->properties, result.randomState,itemLevel);
                    result.randomState = properties.randomState;
                    affix.propertyRolls = std::move(properties.values);
                    affix.operationRolls=std::move(properties.operations);
                    break;
                }
            }
        }
    }
    if (quality == ItemQuality::Crafted)
        result.generation.requiredLevel = std::min(98, std::max(result.generation.requiredLevel,
            item.base.requiredLevel.value_or(0)) + 10 + 3 * int(result.generation.affixes.size()));
    if (result.generation.affixes.empty())
        result.deferred = "No eligible original affix: " + item.code;
    result.generation.propertyRandom=result.randomState;
    return result;
}
} // namespace d2x
