#include "region_generation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
std::optional<int> choosePopulationMonster(std::span<const MonsterPopulationChoice> roster,
    std::span<const int> normalUniques,int difficulty,bool elite,uint64_t &roomRandom) {
    if(elite && difficulty==0) {
        if(normalUniques.empty()) return {};
        return normalUniques[limitedRandom(roomRandom,uint32_t(normalUniques.size()))];
    }
    if(roster.empty()) return {};
    uint32_t rarity=0;
    for(const auto &entry:roster) {
        if(entry.rarity<0 || entry.rarity>255) throw std::invalid_argument("Invalid native monster rarity");
        rarity+=uint32_t(entry.rarity);
    }
    int selection=int(limitedRandom(roomRandom,rarity))+1;
    for(const auto &entry:roster) {
        selection-=entry.rarity;
        if(selection>0) continue;
        if(entry.placeSpawn && entry.replacement>=0 && limitedRandom(roomRandom,100)>unsigned(elite?0:20))
            return entry.replacement;
        return entry.definition;
    }
    return {};
}
int choosePopulationGroup(int uniqueCount,int minimum,int maximum,int processedRooms,
    int populatedRooms,uint64_t &roomRandom) {
    if(uniqueCount<minimum && populatedRooms &&
        int(limitedRandom(roomRandom,100))<100*processedRooms/populatedRooms) return 0;
    if(uniqueCount<maximum && limitedRandom(roomRandom,100)<=5) return 0;
    return limitedRandom(roomRandom,100)>35?2:1;
}
std::map<int,MonsterRegionSelection> initializeMonsterRegions(
    const MonsterRegionRules &rules,int difficulty,uint64_t &random) {
    if(difficulty<0 || difficulty>2) throw std::invalid_argument("Invalid monster region difficulty");
    std::map<int,MonsterRegionSelection> result;
    for(const auto &[level,definition]:rules.levels) {
        if(level<=0) continue;
        auto candidates=definition.candidates[difficulty?1:0];
        const int count=std::min({std::max(definition.count,0),13,int(candidates.size())});
        auto &selection=result[level];
        for(int slot=0;slot<count;++slot) {
            size_t index=limitedRandom(random,uint32_t(candidates.size()));
            if(!slot && definition.rangedFirst) {
                for(int attempt=0;attempt<20;++attempt) {
                    const auto found=rules.classes.find(candidates[index]);
                    if(found!=rules.classes.end() && found->second.ranged) break;
                    index=limitedRandom(random,uint32_t(candidates.size()));
                }
            }
            const int selected=candidates[index];
            candidates.erase(candidates.begin()+index);
            const auto found=rules.classes.find(selected);
            if(found!=rules.classes.end() && found->second.spawn) selection.classes.push_back(selected);
        }
        for(const int selected:selection.classes) {
            const auto &definition=rules.classes.at(selected);
            if(definition.hasComponents)
                selection.components.emplace(selected,monsterComponentPalette(definition.components,random));
        }
        selection.randomAfter=random;
    }
    return result;
}
}