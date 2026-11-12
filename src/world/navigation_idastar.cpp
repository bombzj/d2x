#include "world/navigation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <vector>

namespace d2x {
namespace {
// D2MOO Path/IDAStar.cpp (MIT; docs/licenses/D2MOO.txt), corrected against
// retail 1.13c Common 70650/70840/708F0/70BB0/70E40. This is the native
// depth-first threshold search, not the ordinary 200-node AStar solver.
constexpr std::array<int,8> offsetX{1,1,0,-1,-1,-1,0,1},offsetY{0,1,1,1,0,-1,-1,-1};
constexpr std::array<std::array<int,8>,8> sequences{{
    {0,1,6,3,4,5,2,7},{0,1,6,3,1,3,6,1},
    {0,1,1,1,1,1,2,7},{1,1,1,1,1,3,6,1},
    {6,4,3,6,1,3,2,7},{7,7,7,7,7,5,2,7},
    {0,7,7,7,7,5,2,7},{0,7,2,5,7,5,2,7}}};
constexpr std::array<int,32> jitter{
    -2,-1,0,1,2,-2,-1,0,1,2,-2,-1,0,1,2,-2,
    -1,0,1,2,-2,-1,0,1,2,-2,-1,0,1,2,-1,1};
int direction(int dx,int dy) {
    // PathMisc::sub_6FDAB770: first of the native three direction choices.
    constexpr std::array<int,25> first{
        5,4,4,4,3,6,5,4,3,2,6,6,6,2,2,6,7,0,1,2,7,0,0,0,1};
    const int ax=std::abs(dx),ay=std::abs(dy);
    if(ax<2*ay) {
        if(ay>=2*ax) {
            if(dx<0) return first[size_t(dy<-1?5:std::min(dy,2)+7)];
            dx&=1;
        }
    } else dy=dy>=0?dy&1:-1;
    dx=std::clamp(dx,-2,2);
    return first[size_t(dy<-1?5*dx+10:std::min(dy,2)+5*dx+12)];
}
struct Node {
    int x{},y{},cost{},heuristic{},evaluations{},sequence{},offset{},next{};
    int parent{-1},child{-1};
};
}
std::deque<Vec> Grid::nativeIdaStarPath(Vec from,Vec to,uint8_t type,MovementCollisionRule rule,
                                     uint8_t targetTolerance,const NativePathSearch &search) const {
    const CollisionQueryScope collisionScope{*this};
    if((type!=0 && type!=16) || (type==16 && !search.random) || rule.size<0 || rule.size>3 ||
       !navigationContains(from) || !std::isfinite(to.x) || !std::isfinite(to.y) ||
       to.x<0 || to.y<0 || to.x>=width || to.y>=height) return {};
    const auto &start=search.startRoom;
    const auto target=search.targetRoom.value_or(start);
    if(start.width<=0 || start.height<=0 || target.width<=0 || target.height<=0) return {};
    int64_t left=start.x,top=start.y,roomWidth=start.width,roomHeight=start.height;
    // Native combines each axis by the room with the greater origin. Do not
    // replace this with a whole-level search or a general rectangle union.
    if(target.x<start.x) {left=target.x;roomWidth=int64_t(start.x)+start.width-target.x;}
    else roomWidth=int64_t(target.x)+target.width-start.x;
    if(target.y<start.y) {top=target.y;roomHeight=int64_t(start.y)+start.height-target.y;}
    else roomHeight=int64_t(target.y)+target.height-start.y;
    if(roomWidth==start.width) {left-=10;roomWidth+=20;}
    if(roomHeight==start.height) {top-=10;roomHeight+=20;}
    if(roomWidth<=0 || roomHeight<=0 || roomWidth>50000 || roomHeight>50000 ||
       roomWidth*roomHeight>50000 || left<INT32_MIN || top<INT32_MIN ||
       left+roomWidth>INT32_MAX || top+roomHeight>INT32_MAX) return {};
    const int minX=int(left),minY=int(top),maxX=int(left+roomWidth),maxY=int(top+roomHeight);
    const int stride=int(roomWidth)+6;
    // Retail reserves 50000 cells but clears the halo too. Allocate the halo
    // explicitly: preserve valid searches without reproducing its overflow.
    std::vector<uint32_t> costs(size_t(stride)*size_t(roomHeight+6));
    std::array<Node,900> nodes{};
    size_t count=1;
    const int goalX=int(std::floor(to.x)),goalY=int(std::floor(to.y));
    const auto heuristic=[&](int x,int y) {
        const int dx=std::abs(goalX-x),dy=std::abs(goalY-y);
        return 2*std::max(dx,dy)+std::min(dx,dy);
    };
    Node root;
    root.x=int(std::floor(from.x));root.y=int(std::floor(from.y));
    // The dispatcher checks the 100-cell limit before target preparation.
    // Do not apply it again to the adjusted target here.
    if(root.x<minX || root.x>maxX || root.y<minY || root.y>maxY) return {};
    root.heuristic=heuristic(root.x,root.y);root.evaluations=-3;
    root.next=direction(goalX-root.x,goalY-root.y);
    int cutoff=root.heuristic;
    if(type==16) cutoff+=cutoff/2;
    const int maximum=std::max(cutoff,int(search.initialFScore));
    const int tolerance=uint8_t(targetTolerance*2);
    const auto nextDirection=[&](Node &node,int previous) {
        // Backtracking from the root's eighth child advances its pointer
        // into the next table row before the exhausted root is discarded.
        const size_t entry=size_t(node.sequence)*8+size_t(node.offset);
        int next=previous+sequences[entry/8][entry%8];
        if(type==16) next+=jitter[rollRandom(*search.random)&31];
        node.next=next&7;
    };
    const auto backtrack=[&](int index) {
        auto &node=nodes[size_t(index)];
        if(node.evaluations<4) {++node.offset;nextDirection(node,node.next);}
        if(++node.evaluations!=5) return index;
        while(index!=0) {
            index=nodes[size_t(index)].parent;
            auto &parent=nodes[size_t(index)];
            ++parent.offset;nextDirection(parent,parent.next);
            if(++parent.evaluations!=5) return index;
        }
        return -1;
    };
    int found=-1;
    do {
        nodes[0]=root;
        int current=0,iterations=0;
        while(current>=0) {
            auto &node=nodes[size_t(current)];
            if(node.x==goalX && node.y==goalY) {found=current;break;}
            if(++iterations>10000) break;
            // Native accepts the last node if the search crossed its room
            // rectangle. It does not expand it, or fabricate the target.
            if(node.x<minX || node.x>maxX || node.y<minY || node.y>maxY) {found=current;break;}
            const int x=node.x+offsetX[size_t(node.next)],y=node.y+offsetY[size_t(node.next)];
            const size_t cell=size_t(y-minY+3)*size_t(stride)+size_t(x-minX+3);
            auto &cached=costs[cell];
            if(!cached && !movementClear(x,y,rule)) cached=1;
            const int cost=node.cost+(x==node.x || y==node.y?2:3);
            // 70A51/70A53: reject a worse cost; D2MOO reverses this test.
            if(!cached || uint32_t(cost)<=cached) {
                cached=uint32_t(cost);
                const int estimate=heuristic(x,y);
                if(cost+estimate<=cutoff) {
                    if(node.child<0) {
                        if(count==nodes.size()) break;
                        node.child=int(count++);nodes[size_t(node.child)]={};
                        nodes[size_t(node.child)].parent=current;
                    }
                    auto &child=nodes[size_t(node.child)];
                    child.x=x;child.y=y;child.cost=cost;child.heuristic=estimate;
                    child.evaluations=0;child.offset=0;
                    child.sequence=(direction(goalX-x,goalY-y)-node.next)&7;
                    // 70B55/70B73 uses the parent's direction, not the
                    // recycled child's old direction from the transcription.
                    nextDirection(child,node.next);
                    current=node.child;
                    if(estimate<tolerance) {found=current;break;}
                    continue;
                }
            }
            current=backtrack(current);
        }
        cutoff+=5;
        if(found>=0 || count==nodes.size()) break;
        count=1; // Coordinate costs survive threshold increments.
    } while(cutoff<maximum);
    if(found<0) return {};
    std::deque<Vec> result;
    int previousX=-2,previousY=-2;
    for(int index=found;nodes[size_t(index)].parent>=0;index=nodes[size_t(index)].parent) {
        const auto &node=nodes[size_t(index)],&parent=nodes[size_t(node.parent)];
        const int dx=node.x-parent.x,dy=node.y-parent.y;
        if(dx!=previousX || dy!=previousY) {
            result.push_front({float(node.x)+.5f,float(node.y)+.5f});
            if(result.size()>=78) return {};
            previousX=dx;previousY=dy;
        }
    }
    // Common 706C8 rejects a single compressed point, even on a clear ray.
    return result.size()>1?result:std::deque<Vec>{};
}
} // namespace d2x
