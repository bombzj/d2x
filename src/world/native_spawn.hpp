#pragma once
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <utility>

namespace d2x {
// COLLISION_GetFreeCoordinatesImpl: side/interior order, Manhattan priority,
// first tie, and an exclusive outer radius. Clear may carry a native room cursor.
template<class Clear> std::optional<std::pair<int,int>> nativeSpawnCoordinate(int x,int y,int maxDistance,const Clear &clear,int increment=1) {
    if(increment<=0) return {};
    if(clear(x,y)) return std::pair{x,y};
    for(int radius=1;radius<maxDistance;radius+=increment) {
        std::optional<std::pair<int,int>> result;int64_t distance=std::numeric_limits<int64_t>::max();
        const auto consider=[&](int column,int row) {
            const bool available=clear(column,row);
            const auto score=std::abs(int64_t(column)-x)+std::abs(int64_t(row)-y);
            if(available && score<distance) {result=std::pair{column,row};distance=score;}
        };
        for(int row=y-radius;row<=y+radius;row+=increment) {consider(x-radius,row);consider(x+radius,row);}
        for(int column=x-radius+1;column<x+radius;column+=increment) {consider(column,y-radius);consider(column,y+radius);}
        if(result) return result;
    }
    return {};
}
}
