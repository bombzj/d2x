#include "placement.hpp"
#include <array>
#include <algorithm>
namespace d2x::pd2 {
std::optional<std::pair<int,int>> scenePlacement(int x,int y,int radius,const std::function<bool(int,int)> &clear) {
    const int bound=std::min(radius,10);
    std::array<bool,400> visited{};visited[210]=true;
    std::array<std::pair<int,int>,400> queue{};queue[0]={10,10};size_t count=1;
    constexpr std::array<std::pair<int,int>,4> neighbors{{{0,-1},{1,0},{0,1},{-1,0}}};
    for(size_t at=0;at<count;++at) {
        const auto [column,row]=queue[at];const int px=x+column*4-40,py=y+row*4-40;
        if(clear(px,py)) return std::pair{px,py};
        for(const auto &[dx,dy]:neighbors) {
            const int nextX=column+dx,nextY=row+dy;
            if(nextX<10-bound || nextY<10-bound || nextX>=10+bound || nextY>=10+bound) continue;
            const size_t index=size_t(nextX*20+nextY);if(visited[index]) continue;
            visited[index]=true;queue[count++]={nextX,nextY};
        }
    }
    return {};
}
}
