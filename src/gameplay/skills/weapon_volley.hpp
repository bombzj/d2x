#pragma once
#include "gameplay/combat/attack_timing.hpp"
#include <vector>
#include <algorithm>
namespace d2x {
struct WeaponVolley { std::vector<int> frames,hits; };
// SUnit::sub_6FCBCFD0 resets elapsed animation time, then schedules the
// remaining native frame flags. Rendering and authority share this clock.
inline WeaponVolley weaponVolley(const WeaponAttackTiming &timing,int count,int rollback) {
    WeaponVolley result;
    count=std::clamp(count,1,255);rollback=std::clamp(rollback,0,100);
    int phase=timing.startFrame*256,elapsed=0,completed=0;
    for(int tick=0;tick<65536 && phase<timing.frames*256;++tick) {
        result.frames.push_back(std::min(phase/256,timing.frames-1));
        const int next=phase+timing.speed;
        if(phase<=timing.actionFrame*256 && next>timing.actionFrame*256 && completed<count) {
            result.hits.push_back(std::max(1,tick));
            if(++completed<count) {elapsed=elapsed*(100-rollback)/100;phase=elapsed*256+timing.speed;continue;}
        }
        phase=next;++elapsed;
    }
    return result;
}
inline int strafeShotCount(int targets,int maximum,int minimum) {
    return targets>0?std::min(maximum,std::max(targets,std::min(minimum,maximum))):0;
}
} // namespace d2x
