#pragma once
#include <string_view>
#include <vector>

namespace d2x {
struct AssassinSequenceFrame {std::string_view mode;int frame{};bool hit{},leftHand{};};
// D2Common SequenceTbls: DragonClaw sequence 16 has separate one/two-claw
// programs. Sequence is an original compiled table, not an MPQ parameter.
inline std::vector<AssassinSequenceFrame> assassinSequence(int sequence,std::string_view weapon) {
    std::vector<AssassinSequenceFrame> result;
    if(sequence==16) {
        if(weapon!="hth" && weapon!="ht1" && weapon!="ht2") return result;
        if(weapon=="ht2") {
            for(int frame=0;frame<=6;++frame) result.push_back({"a2",frame,frame==6,false});
            for(int frame=3;frame<=11;++frame) result.push_back({"s4",frame,frame==6,true});
        } else for(int frame=0;frame<12;++frame) result.push_back({"a2",frame,frame==6,false});
    } else if(sequence==21) {
        for(int frame=0;frame<10;++frame) result.push_back({"sc",frame,frame==9,false});
        for(int frame=0;frame<13;++frame) result.push_back({"kk",frame,frame==4,false});
    } else if(sequence==23) {
        for(int frame=0;frame<10;++frame) result.push_back({"sc",frame,false,false});
        result.push_back({"sc",9,false,false});result.push_back({"sc",9,false,false});
        for(int frame=10;frame<17;++frame) result.push_back({"sc",frame,frame==10,false});
    }
    return result;
}
}
