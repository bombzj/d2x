#pragma once
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <optional>
#include <span>
#include <utility>

namespace d2x {
struct NativeLevelSpawnEntry {int definition{-1};bool randomSpawn{};};
// MONSTERSPAWN_SpawnRandomMonsterForLevel always uses Levels.mon, including
// Nightmare/Hell. Advance the selected index before the first isSpawn probe;
// a failed spawn of the first eligible class never tries a different class.
inline std::optional<int> nativeLevelSpawnClass(std::span<const NativeLevelSpawnEntry> entries,uint64_t &roomRandom) {
    if(entries.empty() || entries.size()>25) return {};
    const auto count=uint32_t(entries.size());
    auto index=limitedRandom(roomRandom,count);
    for(uint32_t attempt=0;attempt<count;++attempt) {
        if(++index>=count) index=0;
        const auto &entry=entries[index];
        if(entry.definition>=0 && entry.randomSpawn) return entry.definition;
    }
    return {};
}
// The non-water branch of SpawnNormalMonster. Retail 1.13c RVA EEF10:
// radius 3,6,...,3*attempts, randomized perimeter start, advance before probe.
// A negative attempts value probes only the origin (and still consumes the
// native orientation/sign draws); zero does not probe at all. Clear supplies
// the original room rectangle, auxiliary spawn check and collision predicate.
template<class Clear> std::optional<std::pair<int,int>> nativeMonsterRingPosition(int x,int y,int attempts,uint64_t &roomRandom,const Clear &clear) {
    const int maximum=attempts<0?0:nativeMultiply(3,attempts);
    for(int64_t ring=attempts<0?0:3;ring<=maximum;ring+=3) {
        const int radius=int(ring);
        int column{},row{},dx{},dy{};
        if(rollRandom(roomRandom)&1) {column=radius;row=int(limitedRandom(roomRandom,uint32_t(radius)));dy=1;}
        else {column=int(limitedRandom(roomRandom,uint32_t(radius)));row=radius;dx=1;}
        if(rollRandom(roomRandom)&1) column=nativeSubtract(0,column);
        if(rollRandom(roomRandom)&1) row=nativeSubtract(0,row);
        column=nativeAdd(x,column);row=nativeAdd(y,row);
        const int left=nativeSubtract(x,radius),right=nativeAdd(x,radius),top=nativeSubtract(y,radius),bottom=nativeAdd(y,radius);
        const auto steps=radius?int64_t(radius)*8:1;
        for(int64_t step=0;step<steps;++step) {
            if(column==left && row==top) {dx=1;dy=0;}
            if(column==right) {
                if(row==top) {dx=0;dy=1;}
                if(row==bottom) {dx=-1;dy=0;}
            }
            if(column==left) {
                if(row==bottom) {dx=0;dy=-1;}
                if(row==top && column==right && row==bottom) dx=dy=0;
            }
            column=nativeAdd(column,dx);row=nativeAdd(row,dy);
            if(clear(column,row)) return std::pair{column,row};
        }
    }
    return {};
}
}
