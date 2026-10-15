#include "staffmods.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
namespace d2x {
std::vector<std::pair<int,int>> rollStaffmods(int level,int first,bool inferior,int bias,
    const std::function<bool(int)> &eligible,uint64_t &random) {
    const int roll=bias+int(limitedRandom(random,100));
    const int count=roll>90?3:roll>70?2:roll>30 || bias?1:0;
    const int tier=level>36?5:level>24?4:level>18?3:level>11?2:1;
    std::vector<std::pair<int,int>> result;
    std::array<int,3> selected{-1,-1,-1};
    for(int index=0;index<count;++index) {
        const int chance=int(limitedRandom(random,100));
        int group=std::max(1,tier+(chance>80?1:chance>30?0:chance>10?-1:-2));
        if(inferior) group=std::min(group,4);
        int skill=-1;
        for(int attempt=0;attempt<6;++attempt) {
            skill=first+5*(group-1)+int(limitedRandom(random,5));
            if(eligible(skill) && std::find(selected.begin(),selected.end(),skill)==selected.end()) {
                selected[size_t(index)]=skill;
                break;
            }
        }
        int rank=1;
        if(!inferior) {const int amount=bias/2+int(limitedRandom(random,100));rank=amount>=90?3:amount>=60?2:1;}
        const auto existing=std::find_if(result.begin(),result.end(),[&](const auto &entry){return entry.first==skill;});
        if(existing==result.end()) result.emplace_back(skill,rank);
        else existing->second=rank;
    }
    return result;
}
}
