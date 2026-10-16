#include "timeline.hpp"
#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace d2x {
ActionTimeline playerActionTimeline(int frames,int speed,int startFrame,std::span<const uint8_t> flags) {
    if(frames<=0 || frames>144 || speed<0 || startFrame<0 || startFrame>=frames || flags.size()<std::size_t(frames))
        throw std::invalid_argument("Missing native player action frames");
    ActionTimeline result;
    if(!speed) {result.end=1;return result;}
    int frame=0,index=startFrame,parameter=0;
    for(int64_t phase=int64_t(startFrame)*256+speed;phase<int64_t(frames)*256;phase+=speed) {
        ++frame;
        for(const auto last=int(phase>>8);index<=last && index<144;++index) {
            const auto event=flags[std::size_t(index)];
            if(event==1 || event==2 || event==4) result.events.push_back({frame,event,parameter++});
            else if(event==3) result.events.push_back({frame,event,0});
        }
    }
    result.end=std::max(frame,1)+1;
    return result;
}
ActionTimeline rewoundPlayerActionTimeline(int frames,int speed,int elapsed,std::span<const uint8_t> flags) {
    if(frames<=0 || frames>144 || speed<0 || flags.size()<std::size_t(frames))
        throw std::invalid_argument("Missing native rewound action frames");
    ActionTimeline result;
    if(!speed) {result.end=1;return result;}
    int frame=0,index=elapsed-1;
    for(int64_t phase=int64_t(elapsed)*256+speed;phase<int64_t(frames)*256;phase+=speed) {
        ++frame;
        for(const auto last=int(phase>>8);index<=last;++index) {
            // A zero elapsed anchor starts at -1 in the original. Only
            // actual AnimData/sequence entries can contribute action flags.
            if(index<0 || index>=frames) continue;
            const auto event=flags[std::size_t(index)];
            if(event>=1 && event<=4) result.events.push_back({frame,event,0});
        }
    }
    result.end=std::max(frame,1)+1;
    return result;
}
}
