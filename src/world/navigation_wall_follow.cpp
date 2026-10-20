#include "navigation.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace d2x {
namespace {
// Adapted from D2MOO PathWF.cpp (MIT, docs/licenses/D2MOO.txt), including
// its 1.13c direction order, alternating wall walks and line simplification.
// Coordinates are local prepared-grid cells; no native room or unit pointers.
struct Cell {
    int x{}, y{};
    bool operator==(const Cell &) const = default;
    Cell operator-(Cell other) const { return {x-other.x,y-other.y}; }
};
constexpr std::array<Cell,8> offsets{{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
constexpr std::array<int,9> adjacent{7,0,1,6,-1,2,5,4,3};
constexpr std::array<std::array<int,8>,2> previous{{{6,6,0,0,2,2,4,4},{2,2,4,4,6,6,0,0}}};
constexpr std::array<std::array<int,8>,2> next{{{2,2,4,4,6,6,0,0},{6,0,0,2,2,4,4,6}}};
struct WallWalk {
    int direction{}, sync{};
    Cell current;
    bool finished{};
    std::vector<Cell> points;
};
int64_t squaredDistance(Cell a,Cell b) {
    const int64_t x=a.x-b.x,y=a.y-b.y;
    return x*x+y*y;
}
std::deque<Vec> simplify(const std::vector<Cell> &points,Cell start,size_t count) {
    std::deque<Vec> result;
    const auto append=[&](Cell p){result.push_back({float(p.x)+.5f,float(p.y)+.5f});};
    if(!count) return result;
    auto delta=points.front()-start;
    int line=0;
    for(size_t i=0;i+1<count;++i) {
        auto step=points[i+1]-points[i];
        if(step==delta) ++line;
        else if(line<=0 && delta.x!=step.x && delta.y!=step.y) {step.x=-2;line=1;}
        else {append(points[i]);line=0;}
        delta=step;
    }
    append(points[count-1]);
    return result;
}
bool detour(const Grid &grid,MovementCollisionRule rule,Cell start,Cell target,Cell subStart,
            std::vector<Cell> &points,size_t &index,int limit,int major) {
    const auto delta=points[index]-subStart;
    if(std::abs(delta.x)>1 || std::abs(delta.y)>1 || delta==Cell{}) return false;
    const int direction=adjacent[size_t(4+3*delta.y+delta.x)];
    std::array<WallWalk,2> walks;
    walks[0].direction=(direction+1)&7;walks[1].direction=(direction+7)&7;
    walks[0].current=walks[1].current=subStart;
    size_t side=0;
    while(!walks[0].finished && !walks[1].finished) {
        auto &walk=walks[side];const auto &other=walks[1-side];
        Cell proposed;
        bool clear=false;
        for(int attempt=0;attempt<4;++attempt) {
            const auto offset=offsets[size_t(walk.direction)];
            proposed={walk.current.x+offset.x,walk.current.y+offset.y};
            if(grid.movementClear(proposed.x,proposed.y,rule)) {clear=true;break;}
            if(attempt<3) walk.direction=next[side][size_t(walk.direction)];
        }
        if(!clear) walk.finished=true;
        else {
            if(!walk.points.empty()) {
                const int advance=major==0?subStart.y-proposed.y:major==1?proposed.x-subStart.x:
                    major==2?proposed.y-subStart.y:subStart.x-proposed.x;
                if(advance>0) {
                    const size_t rejoin=index+size_t(advance-1);
                    if(rejoin<points.size() && proposed==points[rejoin]) {
                        walk.points.push_back(proposed);
                        points.erase(points.begin()+index,points.begin()+rejoin+1);
                        points.insert(points.begin()+index,walk.points.begin(),walk.points.end());
                        index+=walk.points.size();
                        return true;
                    }
                }
            }
            if(other.points.size()>1) {
                if(walk.sync>0) {
                    if(proposed==other.points[size_t(walk.sync-2)]) {points.resize(index);return false;}
                    walk.sync=0;
                }
                if(proposed==other.current) walk.sync=int(other.points.size());
            }
            walk.direction=previous[side][size_t(walk.direction)];
            walk.points.push_back(proposed);walk.current=proposed;
            if(limit-int(points.size())-1<=int(walk.points.size())) walk.finished=true;
        }
        if(!walk.sync) side=1-side;
    }
    if(limit>80) {points.resize(index);return false;}
    const auto distance=[&](const WallWalk &walk) {
        return walk.points.empty()?std::numeric_limits<int64_t>::max():squaredDistance(walk.points.back(),target);
    };
    const auto &best=distance(walks[0])<distance(walks[1])?walks[0]:walks[1];
    if(squaredDistance(start,target)<distance(best)) return false;
    points.resize(index);
    points.insert(points.end(),best.points.begin(),best.points.end());
    index=points.size();
    return !walks[side].points.empty();
}
}
std::deque<Vec> Grid::nativeWallFollowPath(Vec from,Vec to,uint8_t maxDistance,MovementCollisionRule rule) const {
    const CollisionQueryScope collisionScope{*this};
    if(!walkable(from,rule) || !std::isfinite(to.x) || !std::isfinite(to.y) ||
       to.x<0 || to.y<0 || to.x>=width || to.y>=height) return {};
    const Cell start{int(std::floor(from.x)),int(std::floor(from.y))},target{int(std::floor(to.x)),int(std::floor(to.y))};
    const int dx=std::abs(target.x-start.x),dy=std::abs(target.y-start.y);
    const int sx=target.x<start.x?-1:1,sy=target.y<start.y?-1:1;
    const bool horizontal=dx>=dy;
    const int count=std::max(dx,dy),minor=std::min(dx,dy);
    // PATH_BresenhamLine reserves one sentinel; WallFollow rejects <=2 steps.
    if(count<=2 || count>=maxDistance) return {};
    const int major=horizontal?(sx>0?1:3):(sy>0?2:0);
    std::vector<Cell> points;
    Cell current=start;int deviation=0;
    for(int step=0;step<count;++step) {
        deviation+=minor;
        if(horizontal) current.x+=sx;else current.y+=sy;
        if(deviation>=count) {deviation-=count;if(horizontal) current.y+=sy;else current.x+=sx;}
        points.push_back(current);
    }
    auto subStart=start;
    for(size_t index=0;index<points.size();++index) {
        if(!movementClear(points[index].x,points[index].y,rule) &&
           !detour(*this,rule,start,target,subStart,points,index,int(maxDistance)-int(index),major))
            return simplify(points,start,std::min(index,points.size()));
        if(index<points.size()) subStart=points[index];
    }
    return simplify(points,start,points.size());
}
}
