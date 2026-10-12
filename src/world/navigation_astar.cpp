#include "world/navigation.hpp"
#include <algorithm>
#include <array>
#include <vector>

namespace d2x {
std::deque<Vec> Grid::nativeAStarPath(Vec from, Vec to, MovementCollisionRule rule, bool unitTarget) const {
    if (!walkable(from, rule) || !std::isfinite(to.x) || !std::isfinite(to.y) ||
        to.x < 0 || to.y < 0 || to.x >= width || to.y >= height) return {};
    // D2MOO Path/AStar.cpp (MIT, docs/licenses/D2MOO.txt): bounded 200-node
    // search, 2/3 costs, native neighbour/tie order and direction compression.
    // Storage/cache representation is local; it never owns an actor or a map.
    constexpr std::array<std::array<int,2>,8> offsets{{
        {-1,-1},{-1,1},{1,-1},{1,1},{-1,0},{0,-1},{1,0},{0,1}}};
    const int gx=int(to.x), gy=int(to.y);
    if (unitTarget && std::none_of(offsets.begin(), offsets.end(), [&](auto d) {
        return movementClear(gx+2*d[0],gy+2*d[1],rule);
    })) return {};
    struct Node {
        int x{}, y{}, cost{}, heuristic{}, parent{-1};
        std::vector<int> children;
        bool visited{};
        int score() const { return cost+heuristic; }
    };
    auto heuristic=[&](int x,int y) {
        const int dx=std::abs(x-gx),dy=std::abs(y-gy);
        return 2*std::max(dx,dy)+std::min(dx,dy);
    };
    std::vector<Node> nodes;
    nodes.reserve(200);
    nodes.push_back({int(from.x),int(from.y),0,heuristic(int(from.x),int(from.y)),-1,{},false});
    std::vector<int> open{0};
    int best=0;
    bool full=false;
    auto improve=[&](int child,int parent,int cost) {
        nodes[child].parent=parent;
        nodes[child].cost=cost;
    };
    while (!open.empty()) {
        const int current=open.front();open.erase(open.begin());
        auto &at=nodes[current];at.visited=true;
        if (at.heuristic<nodes[best].heuristic ||
            (at.heuristic==nodes[best].heuristic && at.cost>nodes[best].cost+5)) best=current;
        if (!at.heuristic) break;
        for (auto d:offsets) {
            const int x=at.x+d[0],y=at.y+d[1];
            if (!movementClear(x,y,rule)) continue;
            const int cost=at.cost+(d[0] && d[1]?3:2);
            auto found=std::find_if(nodes.begin(),nodes.end(),[&](const auto &n){return n.x==x && n.y==y;});
            if (found!=nodes.end()) {
                const int child=int(found-nodes.begin());at.children.push_back(child);
                if (cost>=found->cost) continue;
                improve(child,current,cost);
                // Native pending nodes keep their position in the sorted list.
                // Improving a visited node propagates to its explored children.
                if (found->visited) {
                    std::vector<int> stack{child};
                    while (!stack.empty()) {
                        const int parent=stack.back();stack.pop_back();
                        for (int next:nodes[parent].children) {
                            const int distance=nodes[parent].cost+
                                (nodes[parent].x==nodes[next].x || nodes[parent].y==nodes[next].y?2:3);
                            if (distance<nodes[next].cost) {improve(next,parent,distance);stack.push_back(next);}
                        }
                    }
                }
            } else {
                if (nodes.size()==200) {full=true;break;}
                const int child=int(nodes.size());at.children.push_back(child);
                nodes.push_back({x,y,cost,heuristic(x,y),current,{},false});
                const auto insert=std::find_if(open.begin(),open.end(),[&](int n){return nodes[n].score()>=nodes[child].score();});
                open.insert(insert,child);
            }
        }
        if (full) break;
    }
    std::deque<Vec> result;
    int previousX=-2,previousY=-2;
    for (int at=best;nodes[at].parent>=0;at=nodes[at].parent) {
        const auto &node=nodes[at];const auto &parent=nodes[node.parent];
        const int dx=node.x-parent.x,dy=node.y-parent.y;
        if (dx!=previousX || dy!=previousY) {
            result.push_front({float(node.x)+.5f,float(node.y)+.5f});
            if (result.size()>=78) return {};
            previousX=dx;previousY=dy;
        }
    }
    return result;
}
} // namespace d2x
