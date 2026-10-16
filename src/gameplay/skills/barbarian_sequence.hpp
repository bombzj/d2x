#pragma once
#include <vector>
#include <string_view>
namespace d2x {
struct BarbarianSequenceFrame { std::string_view mode;int frame{};bool hit{},leftHand{}; };
inline std::vector<BarbarianSequenceFrame> barbarianSequence(int sequence,std::string_view weapon) {
    (void)weapon;
    std::vector<BarbarianSequenceFrame> result;
    if(sequence==4) {
        for(int i=0;i<8;++i) result.push_back({"rn",i,true,false});
        constexpr int attack[]{1,4,5,6,8,10,12};
        for(int i=0;i<7;++i) result.push_back({"a1",attack[i],i==3,false});
    } else if(sequence==10) {
        constexpr int frames[]{0,1,2,3,3,4,5,6};
        for(int i=0;i<8;++i) result.push_back({"a1",frames[i],i==3 || i==7,false});
    } else if(sequence==11) {
        constexpr int first[]{1,2,4,6,7,9,11,13,14};
        constexpr int second[]{2,4,5,6,8,9,10,11};
        for(int i=0;i<9;++i) result.push_back({"a1",first[i],i==4,false});
        for(int i=0;i<8;++i) result.push_back({"s3",second[i],i==0,true});
    } else if(sequence==14) {
        for(int i=0;i<15;++i) result.push_back({"s1",i,i==5 || i==11,false});
        const bool shortAttack=weapon=="hth" || weapon=="1js" || weapon=="1jt" || weapon=="1ss" || weapon=="1st" || weapon=="ht1" || weapon=="ht2";
        const int first=shortAttack?5:6,last=shortAttack?11:weapon=="bow" || weapon=="1ht"?15:weapon=="stf"?17:18;
        const int hit=weapon=="stf"?8:shortAttack?6:7;
        for(int i=first;i<=last;++i) result.push_back({"a1",i,i==hit,false});
    } else if(sequence==15) {
        constexpr int first[]{2,3,4,6,7,8};constexpr int second[]{2,4,5,7,8,10};
        for(int i=0;i<6;++i) result.push_back({"th",first[i],i==5,false});
        for(int i=0;i<6;++i) result.push_back({"s3",second[i],i==3,true});
    }
    return result;
}
}
