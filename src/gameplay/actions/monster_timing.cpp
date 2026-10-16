#include "monster_timing.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
MonsterAnimationStep advanceMonsterAnimation(MonsterAnimationCursor &cursor,int frames,int speed,int frameBonus,
    bool sequence,std::span<const uint8_t> flags) {
    if(frames<=0 || speed<0 || flags.size()<size_t(frames) || frameBonus<0 || frameBonus>=frames)
        throw std::invalid_argument("Missing native monster animation frames");
    MonsterAnimationStep result;const int32_t count=frames*256;
    const auto read=[&](int index) {if(index>=0 && index<frames && flags[size_t(index)]>=1 && flags[size_t(index)]<=4) result.event=flags[size_t(index)];};
    if(sequence) {
        const auto old=cursor.frame;cursor.frame+=speed;
        if(cursor.frame>=count) cursor.frame-=count;
        cursor.remaining-=speed;
        if(old==cursor.frame) read(cursor.frame>>8);
        else for(int index=(old>>8)+1;index<=cursor.frame>>8;++index) read(index);
        result.ended=cursor.remaining<=0;
    } else {
        int index=(cursor.frame>>8)+(speed>=256?1:0);cursor.frame+=speed;
        while(cursor.frame>=count) {
            for(;index<frames;++index) read(index);
            index=0;cursor.frame-=count;cursor.frame+=frameBonus*256;
        }
        for(;index<=cursor.frame>>8;++index) read(index);
        result.ended=cursor.frame+speed>=count;
    }
    return result;
}
MonsterActionTiming planMonsterActionTiming(const MonsterActionTimingInput &input) {
    if (input.fallbackRate <= 0) throw std::invalid_argument("Unprepared monster action rate");
    const auto scaled = [&](int frames) {
        return uint64_t((int64_t(frames) * 100 + input.fallbackRate - 1) / input.fallbackRate);
    };
    MonsterActionTiming result{scaled(input.duration), scaled(input.release), 0, {}};
    if (input.animation) {
        const auto animation = *input.animation;
        if (input.casting) result.speed = effectiveCastSpeed(animation.speed, input.fasterCast);
        else if (input.attackRate) result.speed = effectiveAttackSpeed(animation.speed, input.itemIAS, input.weaponSpeed, input.skillRate);
        else if (input.sequence) result.speed = animation.speed;
        else result.speed = effectiveOtherAnimationSpeed(animation.speed, input.otherRate);
        result.duration = uint64_t(monsterAnimationTicks({animation.frames, result.speed}, input.startFrame));
        result.release = uint64_t(std::clamp(animationFrameTicks(std::max(0, input.actionFrame - input.startFrame), result.speed), 1, int(result.duration)));
    }
    if (result.speed && !input.animationEvents.empty()) {
        for (const int frame : input.animationEvents)
            result.events.push_back(std::max(1, animationFrameTicks(frame, result.speed)));
    } else {
        for (const int frame : input.releaseFrames) result.events.push_back(int(scaled(frame)));
    }
    return result;
}
}
