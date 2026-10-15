#include "unique_modifiers.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
std::vector<int> chooseMonsterModifiers(std::span<const MonsterModifierChoice> choices,
    std::span<const int> existing,int count,uint64_t &random) {
    if(existing.size()>9 || count<0) throw std::invalid_argument("Invalid native modifier slots");
    std::vector<int> selected(existing.begin(),existing.end());
    for(int pick=0;pick<count && selected.size()<9;++pick) {
        uint32_t total=0;
        size_t eligible=0;
        for(const auto &entry:choices) {
            if(entry.id<=0 || entry.id>=256 || entry.weight<0 || entry.weight>UINT16_MAX)
                throw std::invalid_argument("Invalid native modifier choice");
            if(!entry.weight || std::find(selected.begin(),selected.end(),entry.id)!=selected.end()) continue;
            total+=uint32_t(entry.weight);
            if(++eligible==256) break;
        }
        auto roll=limitedRandom(random,total);
        if(!total) break;
        for(const auto &entry:choices) {
            if(!entry.weight || std::find(selected.begin(),selected.end(),entry.id)!=selected.end()) continue;
            if(roll<uint32_t(entry.weight)) {selected.push_back(entry.id);break;}
            roll-=uint32_t(entry.weight);
        }
    }
    return selected;
}
}