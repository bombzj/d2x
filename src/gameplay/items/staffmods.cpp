#include "staffmods.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
std::vector<std::pair<int,int>> rollStaffmods(int level,int first,bool inferior,int bias,
    const std::function<bool(int)> &eligible,uint64_t &random) {
    const int roll=bias+int(limitedRandom(random,100));
    const int count=roll>90?3:roll>70?2:roll>30 || bias?1:0;
    const int tier=level>36?5:level>24?4:level>18?3:level>11?2:1;
    std::vector<std::pair<int,int>> result;
    for(int index=0;index<count;++index) {
        const int chance=int(limitedRandom(random,100));
        int group=std::max(1,tier+(chance>80?1:chance>30?0:chance>10?-1:-2));
        if(inferior) group=std::min(group,4);
        int skill=-1;
        for(int attempt=0;attempt<6;++attempt) {
            const int next=first+5*(group-1)+int(limitedRandom(random,5));
            if(eligible(next) && std::none_of(result.begin(),result.end(),[&](const auto &v){return v.first==next;})) {skill=next;break;}
        }
        if(skill<0) throw std::runtime_error("Original staffmod selection exhausted eligible skills");
        int rank=1;
        if(!inferior) {const int amount=bias/2+int(limitedRandom(random,100));rank=amount>=90?3:amount>=60?2:1;}
        result.emplace_back(skill,rank);
    }
    return result;
}
}
