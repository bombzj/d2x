#include "navigation_planner.hpp"
#include "navigation_snapshot.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>

namespace d2x {
namespace {
thread_local const std::function<void()> *workCheckpoint{};
thread_local NavigationDerivedCache *derivedCache{};
class PlanningMemory {
        NavigationDerivedCache *cache_=derivedCache;
        size_t bytes_{};
    public:
        ~PlanningMemory() {if(cache_) cache_->activeBytes-=bytes_;}
        bool reserve(size_t bytes) {
                constexpr size_t limit=64*1024*1024;
                const size_t used=cache_?cache_->activeBytes:bytes_;
                if(bytes>limit || used>limit-bytes) return false;
                bytes_+=bytes;
                if(cache_) {cache_->activeBytes+=bytes;cache_->peakActiveBytes=std::max(cache_->peakActiveBytes,cache_->activeBytes);}
                return true;
        }
        PlanningMemory()=default;
        PlanningMemory(const PlanningMemory &)=delete;
        PlanningMemory &operator=(const PlanningMemory &)=delete;
};
constexpr std::array<std::pair<int,int>,8> walkingDirections{{{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}}};
std::shared_ptr<const NavigationDerivedCache::Block> walkingBlock(const Grid &grid,int blockX,int blockY,int originX,int originY,
        NavigationDerivedCache &cache) {
    const std::pair key{blockX,blockY};
    std::array<uint16_t,36*36> masks;
    for(int row=0;row<36;++row) for(int column=0;column<36;++column)
        masks[row*36+column]=grid.movementMask(blockX*32+column-2-originX,blockY*32+row-2-originY);
    const auto old=cache.walking.find(key);
    if(old!=cache.walking.end() && old->second->masks==masks) {++cache.hits;return old->second;}
    auto block=std::make_shared<NavigationDerivedCache::Block>();block->masks=masks;block->used=++cache.sequence;
    Grid local;local.width=36;local.height=36;
    local.collisionSample=[&masks](int column,int row,EntityId)->uint16_t {
        return column<0 || row<0 || column>=36 || row>=36?0xffff:masks[row*36+column];
    };
    std::array<uint16_t,1024> parent;
    for(size_t cell=0;cell<parent.size();++cell) parent[cell]=uint16_t(cell);
    const auto root=[&](uint16_t cell) {
        while(parent[cell]!=cell) {parent[cell]=parent[parent[cell]];cell=parent[cell];}
        return cell;
    };
    for(int row=0;row<32;++row) {
        navigationWorkCheckpoint();
        for(int column=0;column<32;++column) {
            const Vec from{float(column)+2.5f,float(row)+2.5f};
            if(!local.walkable(from,playerMovement)) continue;
            const int cell=row*32+column;
            for(size_t direction=0;direction<walkingDirections.size();++direction) {
                const auto [dx,dy]=walkingDirections[direction];
                const Vec to=from+Vec{float(dx),float(dy)};
                if(!local.walkable(to,playerMovement) || !local.segment(from,to,{},playerMovement) ||
                    !local.nativeMovementSegment(from,to,playerMovement)) continue;
                block->edges[cell]|=uint8_t(1u<<direction);
                if(column+dx>=0 && column+dx<32 && row+dy>=0 && row+dy<32)
                    parent[root(uint16_t(cell))]=root(uint16_t((row+dy)*32+column+dx));
            }
        }
    }
    for(size_t cell=0;cell<parent.size();++cell) block->components[cell]=root(uint16_t(cell));
    if(cache.walking.size()>=1024 && !cache.walking.contains(key)) {
        const auto oldest=std::min_element(cache.walking.begin(),cache.walking.end(),[](const auto &left,const auto &right) {
            return left.second->used<right.second->used;
        });
        cache.walking.erase(oldest);
    }
    cache.walking[key]=block;++cache.builds;return block;
}
class PlanningClearance {
    std::map<std::pair<int,int>,std::array<uint8_t,1024>> blocks_;
  public:
    int at(const Grid &grid,int column,int row) {
        const std::pair key{column/32,row/32};
        auto found=blocks_.find(key);
        if(found==blocks_.end()) {
            constexpr int pad=10,side=52;
            std::array<uint8_t,side*side> field;field.fill(9);
            for(int localY=1;localY<side-1;++localY) {
                navigationWorkCheckpoint();
                for(int localX=1;localX<side-1;++localX) {
                    const auto mask=grid.movementMask(key.first*32+localX-pad,key.second*32+localY-pad);
                    if(mask!=0xffff && (mask&playerMovement.mask)) field[localY*side+localX]=0;
                }
            }
            for(int localY=1;localY<side-1;++localY) for(int localX=1;localX<side-1;++localX) {
                const int cell=localY*side+localX;
                field[cell]=std::min<int>(field[cell],1+std::min({field[cell-1],field[cell-side-1],field[cell-side],field[cell-side+1]}));
            }
            for(int localY=side-2;localY>0;--localY) for(int localX=side-2;localX>0;--localX) {
                const int cell=localY*side+localX;
                field[cell]=std::min<int>(field[cell],1+std::min({field[cell+1],field[cell+side-1],field[cell+side],field[cell+side+1]}));
            }
            if(blocks_.size()>=512) blocks_.clear();
            found=blocks_.try_emplace(key).first;
            for(int localY=0;localY<32;++localY) for(int localX=0;localX<32;++localX)
                found->second[localY*32+localX]=field[(localY+pad)*side+localX+pad];
        }
        return found->second[(row%32)*32+column%32];
    }
};
}
NavigationWorkScope::NavigationWorkScope(std::function<void()> checkpoint)
    :checkpoint_(std::move(checkpoint)),previous_(workCheckpoint) {
    workCheckpoint=&checkpoint_;
}
NavigationWorkScope::~NavigationWorkScope() {workCheckpoint=previous_;}
NavigationCacheScope::NavigationCacheScope(NavigationDerivedCache &cache):previous_(derivedCache) {derivedCache=&cache;}
NavigationCacheScope::~NavigationCacheScope() {derivedCache=previous_;}
void navigationWorkCheckpoint() {
    if(workCheckpoint) (*workCheckpoint)();
}
bool navigationRouteValid(const Grid &grid,std::span<const NavigationRoom> rooms,const NavigationRoute &route,int range,bool restricted) {
    if(route.points.empty() || !grid.walkable(route.origin,playerMovement)) return false;
    const auto roomAt=[&](Vec at) {
        for(size_t index=0;index<rooms.size();++index) {
            const auto &room=rooms[index];
            if(at.x>=room.x && at.y>=room.y && at.x<double(room.x)+room.width && at.y<double(room.y)+room.height) return index;
        }
        return rooms.size();
    };
    Vec previous=route.origin;
    for(const auto next:route.points) {
        if(!grid.walkable(next,playerMovement)) return false;
        if(range>0) {
            const auto source=roomAt(previous),destination=roomAt(next);
            const double dx=double(next.x)-previous.x,dy=double(next.y)-previous.y;
            if(source>=rooms.size() || destination>=rooms.size() || dx*dx+dy*dy>double(range)*range ||
                (source!=destination && std::find(rooms[source].near.begin(),rooms[source].near.end(),destination)==rooms[source].near.end()) ||
                (restricted && !grid.interactionSegment(previous-Vec{.5f,.5f},next-Vec{.5f,.5f},2,{},0x0804,2))) return false;
        } else if(!grid.segment(previous,next,{},playerMovement) || !grid.nativeMovementSegment(previous,next,playerMovement)) return false;
        previous=next;
    }
    return true;
}
NavigationRoomTree planRoomTree(const Grid &grid,const std::vector<NavigationPlanningRoom> &rooms,
        size_t root,int level,int range,Vec origin,uint64_t geometry,NavigationPassageCache &cache,
        const std::shared_ptr<std::atomic<bool>> &cancel) {
    const auto check=[&] {
        navigationWorkCheckpoint();
        if(cancel && cancel->load(std::memory_order_relaxed)) throw std::runtime_error("Navigation cancelled");
    };
    if(rooms.size()>65536 || range<0 || range>65535 || !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
        std::abs(origin.x)>65536 || std::abs(origin.y)>65536)
        throw std::invalid_argument("Invalid room tree input");
    for(const auto &room:rooms) if(room.width<=0 || room.height<=0 || room.width>65536 || room.height>65536 ||
        int64_t(room.x)<-65536 || int64_t(room.y)<-65536 || int64_t(room.x)+room.width>131072 ||
        int64_t(room.y)+room.height>131072 || uint64_t(room.width+2)*(room.height+2)>512*1024)
        throw std::invalid_argument("Invalid room tree geometry");
    NavigationRoomTree result;result.root=root;result.geometry=geometry;
    result.previous.assign(rooms.size(),rooms.size());result.depth.resize(rooms.size());
    if(root>=rooms.size()) return result;
    std::vector<size_t> pending{root};result.previous[root]=root;
    const auto snapshot=grid.freezeMovement?grid.freezeMovement():std::shared_ptr<const NavigationSnapshot>{};
    const auto passage=[&](const NavigationPlanningRoom &first,const NavigationPlanningRoom &second) {
        const NavigationRoom firstLocal{first.x-int(origin.x),first.y-int(origin.y),first.width,first.height,{}};
        const NavigationRoom secondLocal{second.x-int(origin.x),second.y-int(origin.y),second.width,second.height,{}};
        if(range>0) {
            if(first.loaded && second.loaded) {
                const NavigationPassageCache::Key key{level,first.x,first.y,first.width,first.height,
                    second.x,second.y,second.width,second.height,range};
                const auto old=cache.entries.find(key);
                const auto prior=old!=cache.entries.end()?old->second.snapshot.lock():std::shared_ptr<const NavigationSnapshot>{};
                if(snapshot && prior &&
                    prior->sameGlobalRegion(*snapshot,first.x-1,first.y-1,first.x+first.width,first.y+first.height) &&
                    prior->sameGlobalRegion(*snapshot,second.x-1,second.y-1,second.x+second.width,second.y+second.height)) {
                    old->second.snapshot=snapshot;
                    return old->second.connected;
                }
                std::vector<uint16_t> masks;
                for(const auto &room:{firstLocal,secondLocal}) for(int row=room.y-1;row<=room.y+room.height;++row) {
                    check();
                    for(int column=room.x-1;column<=room.x+room.width;++column) masks.push_back(grid.movementMask(column,row));
                }
                if(const auto current=cache.entries.find(key);current!=cache.entries.end() && current->second.masks==masks) {
                    current->second.snapshot=snapshot;return current->second.connected;
                }
                std::map<int,std::vector<int>> rows;
                for(int row=std::max(secondLocal.y,firstLocal.y-range);row<std::min(secondLocal.y+secondLocal.height,firstLocal.y+firstLocal.height+range);++row) {
                    check();
                    for(int column=std::max(secondLocal.x,firstLocal.x-range);column<std::min(secondLocal.x+secondLocal.width,firstLocal.x+firstLocal.width+range);++column)
                        if(grid.walkable(column,row,playerMovement)) rows[row].push_back(column);
                }
                bool connected=false;
                for(int row=std::max(firstLocal.y,secondLocal.y-range);!connected && row<std::min(firstLocal.y+firstLocal.height,secondLocal.y+secondLocal.height+range);++row) {
                    check();
                    for(int column=std::max(firstLocal.x,secondLocal.x-range);!connected && column<std::min(firstLocal.x+firstLocal.width,secondLocal.x+secondLocal.width+range);++column) {
                        if(!grid.walkable(column,row,playerMovement)) continue;
                        for(auto other=rows.lower_bound(row-range);!connected && other!=rows.end() && other->first<=row+range;++other) {
                            const auto &columns=other->second;auto next=std::lower_bound(columns.begin(),columns.end(),column);
                            const auto inRange=[&](int target) {
                                const int64_t dx=int64_t(target)-column,dy=int64_t(other->first)-row;
                                return dx*dx+dy*dy<=int64_t(range)*range;
                            };
                            connected=(next!=columns.end() && inRange(*next)) || (next!=columns.begin() && inRange(*std::prev(next)));
                        }
                    }
                }
                if(const auto current=cache.entries.find(key);current!=cache.entries.end()) {
                    cache.bytes-=current->second.masks.size()*sizeof(uint16_t);cache.entries.erase(current);
                }
                const size_t bytes=masks.size()*sizeof(uint16_t);
                if(bytes<=16*1024*1024) {
                    if(cache.bytes+bytes>16*1024*1024) {cache.entries.clear();cache.bytes=0;}
                    cache.bytes+=bytes;cache.entries.emplace(key,NavigationPassageCache::Evidence{std::move(masks),connected,snapshot});
                }
                return connected;
            }
            const int dx=std::max({0,secondLocal.x-firstLocal.x-firstLocal.width+1,firstLocal.x-secondLocal.x-secondLocal.width+1});
            const int dy=std::max({0,secondLocal.y-firstLocal.y-firstLocal.height+1,firstLocal.y-secondLocal.y-secondLocal.height+1});
            return int64_t(dx)*dx+int64_t(dy)*dy<=int64_t(range)*range;
        }
        bool vertical=false;int plane=0,start=0,end=0;
        if(firstLocal.x+firstLocal.width==secondLocal.x || secondLocal.x+secondLocal.width==firstLocal.x) {
            vertical=true;plane=std::max(firstLocal.x,secondLocal.x);start=std::max(firstLocal.y,secondLocal.y);end=std::min(firstLocal.y+firstLocal.height,secondLocal.y+secondLocal.height);
        } else if(firstLocal.y+firstLocal.height==secondLocal.y || secondLocal.y+secondLocal.height==firstLocal.y) {
            plane=std::max(firstLocal.y,secondLocal.y);start=std::max(firstLocal.x,secondLocal.x);end=std::min(firstLocal.x+firstLocal.width,secondLocal.x+secondLocal.width);
        } else return false;
        if(start>=end) return false;
        if(!first.loaded && !second.loaded) return true;
        for(int along=start;along<end;++along) {
            check();bool blocked=false,unknown=false;
            for(int side:{-1,0}) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                if(!nativeCollisionSizeCell(playerMovement.size,dx,dy)) continue;
                const auto mask=grid.movementMask((vertical?plane+side:along)+dx,(vertical?along:plane+side)+dy);
                if(mask==0xffff) unknown=true;else if(mask&playerMovement.mask) blocked=true;
            }
            if(blocked) continue;
            if(unknown) return true;
            const Vec from{float(vertical?plane-1:along)+.5f,float(vertical?along:plane-1)+.5f};
            if(grid.nativeMovementSegment(from,from+(vertical?Vec{1,0}:Vec{0,1}),playerMovement)) return true;
        }
        return false;
    };
    for(size_t head=0;head<pending.size();++head) {
        check();const auto index=pending[head];const auto &room=rooms[index];
        if(!room.loaded && !room.exploredTiles) continue;
        for(const auto next:room.near) if(next<rooms.size() && rooms[next].level==level &&
            result.previous[next]==rooms.size() && passage(room,rooms[next])) {
            result.previous[next]=index;result.depth[next]=result.depth[index]+1;pending.push_back(next);
        }
    }
    return result;
}
std::vector<NavigationViewpoint> navigationViewpoints(const Grid &grid,const NavigationViewpointInput &input,
    const std::shared_ptr<std::atomic<bool>> &cancel,int spacing) {
    std::vector<NavigationViewpoint> result;
    if(spacing<=0) throw std::invalid_argument("Invalid viewpoint spacing");
    if(grid.width<=0 || grid.height<=0 || size_t(grid.width)*grid.height>16*1024*1024)
        throw std::invalid_argument("Invalid viewpoint domain");
    std::vector<bool> seen(size_t(grid.width)*grid.height,false);
    PlanningClearance clearances;
    const float margin=input.outdoor?0.f:5.f;
    const float halfX=std::max(0.f,input.halfExtent.x-margin),halfY=std::max(0.f,input.halfExtent.y-margin);
    std::map<int,std::vector<int>> boundaryBands;
    if(input.objective==NavigationObjective::PerimeterConnection)
        for(const auto &[diagonal,values]:input.unseenBands) {
            navigationWorkCheckpoint();
            if(cancel && cancel->load(std::memory_order_relaxed)) return {};
            for(const int along:values) {
                const Vec center{float(along+diagonal)*.5f,float(along-diagonal)*.5f};
                if(std::any_of(input.boundaries.begin(),input.boundaries.end(),[&](const auto &edge) {
                    const bool vertical=edge.side%2!=0;
                    return std::abs((vertical?center.x:center.y)-edge.plane)<=5 &&
                        (vertical?center.y:center.x)>=edge.start-5 && (vertical?center.y:center.x)<=edge.end+5;
                })) boundaryBands[diagonal].push_back(along);
            }
        }
    for(int row=spacing/2;!input.unseenBands.empty() && row<grid.height;row+=spacing) {
        navigationWorkCheckpoint();
        if(cancel && cancel->load(std::memory_order_relaxed)) return {};
        for(int column=spacing/2;column<grid.width;column+=spacing) {
            if(column%32==0) {
                navigationWorkCheckpoint();
                if(cancel && cancel->load(std::memory_order_relaxed)) return {};
            }
            const Vec center{float(column)+.5f,float(row)+.5f};
            const auto standing=spacing==1?(grid.walkable(center,playerMovement)?std::optional{center}:std::nullopt):
                grid.nativeSpawn(center,2,playerMovement);
            if(!standing) continue;
            const auto local=*standing,world=local+input.origin-Vec{.5f,.5f};
            const size_t cell=size_t(local.y)*grid.width+int(local.x);
            if(seen[cell]) continue;
            seen[cell]=true;
            if(std::none_of(input.rooms.begin(),input.rooms.end(),[&](const auto &room) {
                return local.x>=room.x && local.y>=room.y && local.x<room.x+room.width && local.y<room.y+room.height;
            })) continue;
            const float span=(world-input.from).length();if(span<4) continue;
            if(std::any_of(input.exclusions.begin(),input.exclusions.end(),[&](const auto &excluded) {
                return (world-excluded.point).length()<excluded.radius;
            })) continue;
            size_t gain=0,boundaryGain=0;const float diagonal=world.x-world.y,along=world.x+world.y;
            for(auto band=input.unseenBands.lower_bound(int(std::ceil(diagonal-halfX)));
                band!=input.unseenBands.end() && band->first<=diagonal+halfX;++band) {
                const auto begin=std::lower_bound(band->second.begin(),band->second.end(),along-halfY);
                const auto end=std::upper_bound(band->second.begin(),band->second.end(),along+halfY);
                gain+=end-begin;
                if(const auto boundary=boundaryBands.find(band->first);boundary!=boundaryBands.end())
                    boundaryGain+=std::upper_bound(boundary->second.begin(),boundary->second.end(),along+halfY)-
                        std::lower_bound(boundary->second.begin(),boundary->second.end(),along-halfY);
            }
            if(!gain) continue;
            float weight=float(gain)/float(gain+boundaryGain);
            if(input.objective==NavigationObjective::PerimeterConnection && !input.boundaries.empty()) {
                float gap=std::numeric_limits<float>::max();
                for(const auto &edge:input.boundaries) {
                    const bool vertical=edge.side%2!=0;
                    const float position=std::clamp(vertical?world.y:world.x,float(edge.start),float(edge.end));
                    gap=std::min(gap,(world-(vertical?Vec{float(edge.plane),position}:Vec{position,float(edge.plane)})).length());
                }
                const float reach=std::max(1.f,std::min(input.halfExtent.x,input.halfExtent.y)*.5f);
                weight+=std::max(0.f,gap-reach)/reach;
            }
            if(input.hub && input.branchScale>0) {
                const auto arm=[&](Vec at) {
                    const Vec delta=at-*input.hub;
                    return std::abs(delta.x)>=std::abs(delta.y)?(delta.x>=0?0:2):(delta.y>=0?1:3);
                };
                const float here=(input.from-*input.hub).length(),there=(world-*input.hub).length();
                if(here>input.branchScale && arm(world)!=arm(input.from)) weight*=4;
                else if(there>here+input.branchScale*.25f) weight*=.65f;
            }
            const int clearance=clearances.at(grid,int(local.x),int(local.y));
            const int preferredClearance=input.mobility==NavigationMobility::TownWalk?4:6;
            const float wall=clearance<preferredClearance?2.f*(preferredClearance-clearance):
                input.mobility==NavigationMobility::TownWalk?0.f:float(clearance-preferredClearance);
            const float castCost=input.cost.teleportCast>0?input.cost.teleportCast:float(input.teleportRange);
            const float cost=input.teleportRange>0?std::ceil(span/input.teleportRange)*castCost:span*input.cost.walkUnit;
            result.push_back({local,gain,(cost+input.cost.observation+wall*input.cost.walkUnit)*weight/float(gain),weight,wall});
            if(result.size()>navigationCandidateLimit) return result;
        }
    }
    std::stable_sort(result.begin(),result.end(),[](const auto &left,const auto &right){return left.score<right.score;});
    return result;
}
NavigationExploration planExploration(const Grid &grid,const NavigationViewpointInput &input,
        const std::shared_ptr<std::atomic<bool>> &cancel) {
    NavigationExploration result;
    PlanningMemory memory;
    if(!memory.reserve(navigationCandidateLimit*(sizeof(NavigationViewpoint)+sizeof(NavigationTarget))+2*1024*1024)) {
        result.selection.route.outcome=NavigationOutcome::ResourceLimited;return result;
    }
    if(!std::isfinite(input.cost.walkUnit) || input.cost.walkUnit<=0 ||
        !std::isfinite(input.cost.teleportCast) || input.cost.teleportCast<0 ||
        !std::isfinite(input.cost.observation) || input.cost.observation<0 ||
        (input.cost.seconds && input.teleportRange>0 && input.cost.teleportCast<=0))
        throw std::invalid_argument("Invalid exploration cost");
    if(grid.width<=0 || grid.height<=0 || size_t(grid.width)*grid.height>16*1024*1024 ||
        input.rooms.size()>65536 || input.exclusions.size()>4096 || input.unseenBands.size()>131072) {
        result.selection.route.outcome=NavigationOutcome::ResourceLimited;return result;
    }
    if(!std::isfinite(input.origin.x) || !std::isfinite(input.origin.y) || !std::isfinite(input.from.x) ||
        !std::isfinite(input.from.y) || !std::isfinite(input.halfExtent.x) || !std::isfinite(input.halfExtent.y) ||
        std::abs(input.origin.x)>65536 || std::abs(input.origin.y)>65536 || std::abs(input.from.x)>131072 ||
        std::abs(input.from.y)>131072 || input.halfExtent.x<0 || input.halfExtent.y<0 ||
        input.halfExtent.x>65536 || input.halfExtent.y>65536 || !std::isfinite(input.branchScale) ||
        input.teleportRange<0 || input.teleportRange>65535)
        throw std::invalid_argument("Invalid exploration input");
    if(input.hub && (!std::isfinite(input.hub->x) || !std::isfinite(input.hub->y)))
        throw std::invalid_argument("Invalid exploration hub");
    if(input.mobility!=NavigationMobility::Walk && input.mobility!=NavigationMobility::TownWalk &&
        input.mobility!=NavigationMobility::Teleport) throw std::invalid_argument("Invalid exploration mobility");
    if(input.objective!=NavigationObjective::Coverage && input.objective!=NavigationObjective::PerimeterConnection &&
        input.objective!=NavigationObjective::InteriorEntrance && input.objective!=NavigationObjective::Units)
        throw std::invalid_argument("Invalid exploration objective");
    if((input.mobility==NavigationMobility::Teleport)!=(input.teleportRange>0))
        throw std::invalid_argument("Exploration mobility and range disagree");
    if(input.objective==NavigationObjective::PerimeterConnection && input.boundaries.empty())
        throw std::invalid_argument("Perimeter exploration requires an observed connection boundary");
    size_t observations=0;
    for(const auto &[diagonal,values]:input.unseenBands) {
        if(diagonal< -262144 || diagonal>262144 ||
            std::any_of(values.begin(),values.end(),[](int value){return value< -262144 || value>262144;}))
            throw std::invalid_argument("Invalid observation coordinate");
        observations+=values.size();
        if(!std::is_sorted(values.begin(),values.end())) throw std::invalid_argument("Unsorted observation band");
    }
    if(observations>512*1024 || input.boundaries.size()>65536) {
        result.selection.route.outcome=NavigationOutcome::ResourceLimited;return result;
    }
    for(const auto &excluded:input.exclusions) if(!std::isfinite(excluded.point.x) || !std::isfinite(excluded.point.y) ||
        !std::isfinite(excluded.radius) || excluded.radius<0) throw std::invalid_argument("Invalid exploration exclusion");
    for(const auto &room:input.rooms) if(room.width<=0 || room.height<=0 || room.width>65536 || room.height>65536 ||
        int64_t(room.x)<-65536 || int64_t(room.y)<-65536 || int64_t(room.x)+room.width>131072 ||
        int64_t(room.y)+room.height>131072) throw std::invalid_argument("Invalid exploration room");
    for(const auto &edge:input.boundaries) if(edge.side<0 || edge.side>3 || edge.start>edge.end ||
        std::abs(int64_t(edge.start))>131072 || std::abs(int64_t(edge.end))>131072 || std::abs(int64_t(edge.plane))>131072)
        throw std::invalid_argument("Invalid exploration boundary");
    result.viewpoints=navigationViewpoints(grid,input,cancel);
    std::vector<NavigationTarget> targets;
    for(const auto &viewpoint:result.viewpoints) targets.push_back({viewpoint.local,viewpoint.weight,viewpoint.gain,viewpoint.penalty});
    result.selection=planNavigation(grid,input.rooms,input.from-input.origin+Vec{.5f,.5f},targets,
        input.teleportRange,input.restricted,cancel,false,input.cost);
    if(result.selection.route.outcome==NavigationOutcome::NoRoute) {
        result.viewpoints=navigationViewpoints(grid,input,cancel,1);targets.clear();
        for(const auto &viewpoint:result.viewpoints) targets.push_back({viewpoint.local,viewpoint.weight,viewpoint.gain,viewpoint.penalty});
        result.selection=planNavigation(grid,input.rooms,input.from-input.origin+Vec{.5f,.5f},targets,
            input.teleportRange,input.restricted,cancel,false,input.cost);
        if(result.selection.route.outcome==NavigationOutcome::NoRoute)
            result.selection.route.outcome=NavigationOutcome::NeedObservation;
    }
    return result;
}
NavigationBacktrack planBacktrack(const Grid &grid,const NavigationBacktrackInput &input,NavigationPassageCache &cache,
        const std::shared_ptr<std::atomic<bool>> &cancel) {
    NavigationBacktrack result;result.destination=input.rooms.size();
    PlanningMemory memory;
    if(!memory.reserve(navigationCandidateLimit*(sizeof(NavigationTarget)+sizeof(size_t))+2*1024*1024)) {
        result.selection.route.outcome=NavigationOutcome::ResourceLimited;return result;
    }
    if(input.priorities.size()!=input.rooms.size() || input.weights.size()!=input.rooms.size() ||
        input.exclusions.size()>4096 || !std::isfinite(input.from.x) || !std::isfinite(input.from.y) ||
        (input.mobility!=NavigationMobility::Walk && input.mobility!=NavigationMobility::TownWalk && input.mobility!=NavigationMobility::Teleport) ||
        (input.mobility==NavigationMobility::Teleport)!=(input.range>0))
        throw std::invalid_argument("Invalid backtrack input");
    for(size_t index=0;index<input.rooms.size();++index)
        if(!std::isfinite(input.priorities[index]) || input.priorities[index]<0 ||
            !std::isfinite(input.weights[index]) || input.weights[index]<=0)
            throw std::invalid_argument("Invalid backtrack weight");
    for(const auto &excluded:input.exclusions)
        if(!std::isfinite(excluded.point.x) || !std::isfinite(excluded.point.y) ||
            !std::isfinite(excluded.radius) || excluded.radius<0)
            throw std::invalid_argument("Invalid backtrack exclusion");
    result.tree=planRoomTree(grid,input.rooms,input.root,input.level,input.range,input.origin,0,cache,cancel);
    const auto &previous=result.tree.previous;
    std::map<size_t,size_t> junctions;
    for(const size_t destination:input.destinations) {
        if(destination>=input.rooms.size()) throw std::invalid_argument("Invalid backtrack destination");
        if(destination==input.root || previous[destination]==input.rooms.size()) continue;
        size_t next=destination;
        while(next!=input.root && (!input.rooms[next].loaded || (input.range>0 && previous[next]!=input.root))) next=previous[next];
        const auto found=junctions.find(next);
        if(found==junctions.end() || input.priorities[destination]<input.priorities[found->second]) junctions[next]=destination;
    }
    result.routeRooms=input.routeRooms;
    std::vector<NavigationTarget> targets;
    std::vector<size_t> destinations;
    PlanningClearance clearances;
    for(const auto &[next,destination]:junctions) {
        const auto &room=input.rooms[next],&other=input.rooms[destination];
        size_t onwardRoom=destination;
        while(onwardRoom!=next && previous[onwardRoom]!=next && previous[onwardRoom]!=input.rooms.size())
            onwardRoom=previous[onwardRoom];
        const auto &onwardGeometry=input.rooms[onwardRoom];
        const Vec onward{float(onwardGeometry.x)+onwardGeometry.width*.5f,float(onwardGeometry.y)+onwardGeometry.height*.5f};
        for(int row=std::max(0,room.y-int(input.origin.y));row<std::min(grid.height,room.y-int(input.origin.y)+room.height);++row) {
            navigationWorkCheckpoint();
            if(cancel && cancel->load(std::memory_order_relaxed)) {
                result.selection.route.outcome=NavigationOutcome::Cancelled;return result;
            }
            for(int column=std::max(0,room.x-int(input.origin.x));column<std::min(grid.width,room.x-int(input.origin.x)+room.width);++column) {
                const Vec local{float(column)+.5f,float(row)+.5f},world=local+input.origin-Vec{.5f,.5f};
                if(!grid.walkable(local,playerMovement) || (world-input.from).length()<=3 ||
                    (next==input.root && (world-onward).length()+2>=(input.from-onward).length()) ||
                    std::any_of(input.exclusions.begin(),input.exclusions.end(),[&](const auto &excluded) {
                        return (world-excluded.point).length()<excluded.radius;
                    })) continue;
                const int clearance=input.clearing?9:clearances.at(grid,column,row);
                const int preferred=input.mobility==NavigationMobility::TownWalk?4:6;
                const float wall=input.clearing?0.f:clearance<preferred?2.f*(preferred-clearance):
                    input.mobility==NavigationMobility::TownWalk?0.f:float(clearance-preferred);
                const float penalty=(float(result.tree.depth[destination])*40+(world-onward).length()+
                    .25f*(world-input.from).length()+(input.range>0?20.f:0.f)+wall)*
                    input.weights[destination]*(other.exploredTiles?1.f:.75f);
                if(targets.size()>=navigationCandidateLimit) {
                    result.candidates=targets.size();
                    result.selection.route.outcome=NavigationOutcome::ResourceLimited;return result;
                }
                targets.push_back({local,1,1,penalty});destinations.push_back(destination);
            }
        }
    }
    result.candidates=targets.size();
    result.selection=planNavigation(grid,result.routeRooms,input.from-input.origin+Vec{.5f,.5f},targets,
        input.range,input.restricted,cancel,false,input.cost);
    if(result.selection.found) result.destination=destinations.at(result.selection.candidate);
    return result;
}
NavigationSelection planNavigation(const Grid &grid,std::span<const NavigationRoom> rooms,Vec from,
        std::span<const NavigationTarget> targets,int teleportRange,bool restricted,
        const std::shared_ptr<std::atomic<bool>> &cancel,bool partial,NavigationCost movementCost) {
    NavigationSelection answer;answer.candidate=targets.size();answer.route.origin=from;
    if(!std::isfinite(movementCost.walkUnit) || movementCost.walkUnit<=0 ||
        !std::isfinite(movementCost.teleportCast) || movementCost.teleportCast<0 ||
        !std::isfinite(movementCost.observation) || movementCost.observation<0 ||
        (movementCost.seconds && teleportRange>0 && movementCost.teleportCast<=0))
        throw std::invalid_argument("Invalid navigation cost");
    const float castCost=movementCost.teleportCast>0?movementCost.teleportCast:float(teleportRange);
    answer.costInSeconds=movementCost.seconds;
    const auto cancelled=[&] {
        navigationWorkCheckpoint();
        return cancel && cancel->load(std::memory_order_relaxed);
    };
    if(cancelled()) {answer.route.outcome=NavigationOutcome::Cancelled;return answer;}
    if(grid.width<=0 || grid.height<=0 || grid.width>65536 || grid.height>65536 ||
        !std::isfinite(from.x) || !std::isfinite(from.y) || from.x<0 || from.y<0 ||
        from.x>=grid.width || from.y>=grid.height) throw std::invalid_argument("Invalid navigation domain or origin");
    if(teleportRange<0 || teleportRange>65535 || targets.size()>navigationCandidateLimit || rooms.size()>65536) {
        answer.route.outcome=NavigationOutcome::ResourceLimited;return answer;
    }
    for(const auto &target:targets) if(!std::isfinite(target.point.x) || !std::isfinite(target.point.y) ||
        !std::isfinite(target.weight) || target.weight<=0 || !std::isfinite(target.penalty) || target.penalty<0 ||
        std::abs(target.point.x)>65536 || std::abs(target.point.y)>65536)
        throw std::invalid_argument("Invalid navigation target");
    for(const auto &room:rooms) if(room.width<=0 || room.height<=0 || room.width>65536 || room.height>65536 ||
        int64_t(room.x)<-65536 || int64_t(room.y)<-65536 || int64_t(room.x)+room.width>131072 ||
        int64_t(room.y)+room.height>131072 || std::any_of(room.near.begin(),room.near.end(),[&](size_t next){return next>=rooms.size();}))
        throw std::invalid_argument("Invalid navigation room");
    if(!grid.walkable(from,playerMovement)) {
        answer.route.outcome=NavigationOutcome::StartBlocked;
        for(int row=-1;row<=1;++row) for(int column=-1;column<=1;++column)
            if(nativeCollisionSizeCell(playerMovement.size,column,row) &&
                grid.movementMask(int(from.x)+column,int(from.y)+row)==0xffff)
                answer.route.outcome=NavigationOutcome::NeedObservation;
        return answer;
    }
    if(targets.empty()) return answer;
    const size_t count=size_t(grid.width)*grid.height;
    if(count>16*1024*1024) {
        answer.route.outcome=NavigationOutcome::ResourceLimited;return answer;
    }
    const size_t none=std::numeric_limits<size_t>::max();
    struct State {
        size_t parent{std::numeric_limits<size_t>::max()};
        float cost{std::numeric_limits<float>::infinity()};
        uint32_t owner{std::numeric_limits<uint32_t>::max()};
    };
    using Page=std::array<State,1024>;
    PlanningMemory memory;
    if(!memory.reserve(((count+1023)/1024)*sizeof(std::unique_ptr<Page>)+targets.size()*128)) {
        answer.route.outcome=NavigationOutcome::ResourceLimited;return answer;
    }
    std::vector<std::unique_ptr<Page>> pages((count+1023)/1024);
    size_t allocatedCells=0;
    bool memoryExhausted=false;
    State unavailable;
    const auto state=[&](size_t cell)->State & {
        auto &page=pages.at(cell/1024);
        if(!page) {
            if(!memory.reserve(sizeof(Page))) {memoryExhausted=true;return unavailable;}
            page=std::make_unique<Page>();allocatedCells+=1024;
        }
        return (*page)[cell%1024];
    };
    const auto resourceLimited=[&] {
        answer.found=false;answer.route.points.clear();answer.route.outcome=NavigationOutcome::ResourceLimited;
        return answer;
    };
    const auto cellAt=[&](Vec at){return size_t(int(at.y))*grid.width+int(at.x);};
    const auto pointAt=[&](size_t cell){return Vec{float(cell%grid.width)+.5f,float(cell/grid.width)+.5f};};
    const auto roomAt=[&](Vec at) {
        for(size_t index=0;index<rooms.size();++index) {
            const auto &room=rooms[index];
            if(at.x>=room.x && at.y>=room.y && at.x<room.x+room.width && at.y<room.y+room.height) return index;
        }
        return rooms.size();
    };
    const auto rootRoom=roomAt(from);
    const auto legalTeleport=[&](Vec start,size_t source,Vec end,size_t destination) {
        if(source>=rooms.size() || destination>=rooms.size()) return false;
        if(source!=destination && std::find(rooms[source].near.begin(),rooms[source].near.end(),destination)==rooms[source].near.end()) return false;
        const double dx=double(end.x)-start.x,dy=double(end.y)-start.y;
        return dx*dx+dy*dy<=double(teleportRange)*teleportRange &&
            (!restricted || grid.interactionSegment(start-Vec{.5f,.5f},end-Vec{.5f,.5f},2,{},0x0804,2));
    };
    std::map<int,std::set<std::pair<int,size_t>>> remaining;
    size_t remainingCount=0;
    for(size_t index=0;index<targets.size();++index) if(grid.walkable(targets[index].point,playerMovement)) {
        remaining[int(targets[index].point.y)].emplace(int(targets[index].point.x),index);++remainingCount;
    }
    std::vector<size_t> targetRooms;
    for(const auto &target:targets) targetRooms.push_back(roomAt(target.point));
    float minimumFactor=std::numeric_limits<float>::infinity();
    for(const auto &target:targets)
        minimumFactor=std::min(minimumFactor,std::max(.001f,target.weight)/float(std::max(size_t{1},target.gain)));
    float best=std::numeric_limits<float>::infinity();
    size_t selected=none,partialCell=none;float partialDistance=std::numeric_limits<float>::infinity();
    const auto consider=[&](size_t cell,Vec at,size_t room,float spent) {
        if(partial && targets.size()==1) {
            const float distance=(targets.front().point-at).length();
            if(distance<partialDistance) {partialDistance=distance;partialCell=cell;}
        }
        const int span=teleportRange>0?teleportRange:0;
        for(auto row=remaining.lower_bound(int(at.y)-span);row!=remaining.end() && row->first<=int(at.y)+span;++row) {
            auto entry=row->second.lower_bound({int(at.x)-span,0});
            while(entry!=row->second.end() && entry->first<=int(at.x)+span) {
                const auto index=entry->second;const auto &target=targets[index];
                const float distance=(target.point-at).length();
                const bool reached=teleportRange>0?legalTeleport(at,room,target.point,targetRooms[index]):
                    (distance<.01f || (int(at.x)==int(target.point.x) && int(at.y)==int(target.point.y) && grid.segment(at,target.point,{},playerMovement) &&
                        grid.nativeMovementSegment(at,target.point,playerMovement)));
                if(!reached) {++entry;continue;}
                const float total=spent+(teleportRange>0?(distance>.01f?castCost:0.f):distance*movementCost.walkUnit);
                const float score=(total+movementCost.observation+target.penalty*movementCost.walkUnit)*
                    std::max(.001f,target.weight)/float(std::max(size_t{1},target.gain));
                entry=row->second.erase(entry);--remainingCount;
                if(score<best) {
                    best=score;selected=cell;answer.candidate=index;answer.found=true;
                    answer.travelCost=total;answer.score=score;
                }
            }
        }
    };
    const auto root=cellAt(from);state(root)={root,0};
    if(memoryExhausted) return resourceLimited();
    consider(root,from,rootRoom,0);
    if(answer.found && !remainingCount) {
        answer.route.outcome=NavigationOutcome::Complete;
        answer.route.points.push_back(targets[answer.candidate].point);
        return answer;
    }
    if(teleportRange>0) {
        if(rootRoom==rooms.size()) return answer;
        size_t standingCells=0;
        std::map<int,std::set<std::pair<int,size_t>>> unseen;
        for(size_t index=0;index<rooms.size();++index) {
            const auto &room=rooms[index];
            for(int row=std::max(0,room.y);row<std::min(grid.height,room.y+room.height);++row) {
                if(cancelled()) {answer.found=false;answer.route.outcome=NavigationOutcome::Cancelled;return answer;}
                for(int column=std::max(0,room.x);column<std::min(grid.width,room.x+room.width);++column) {
                    const size_t cell=size_t(row)*grid.width+column;
                    if(!grid.walkable(column,row,playerMovement)) continue;
                    auto &node=state(cell);
                    if(memoryExhausted || allocatedCells>1024*1024) return resourceLimited();
                    if(node.owner!=std::numeric_limits<uint32_t>::max()) continue;
                    if(standingCells>=512*1024) return resourceLimited();
                    if(!memory.reserve(96)) return resourceLimited();
                    node.owner=uint32_t(index);++standingCells;
                    if(cell!=root) unseen[row].emplace(column,cell);
                }
            }
        }
        std::vector<size_t> queue{root};
        for(size_t head=0;head<queue.size();++head) {
            if(cancelled()) {answer.found=false;answer.route.points.clear();answer.route.outcome=NavigationOutcome::Cancelled;return answer;}
            const size_t current=queue[head];const Vec at=current==root?from:pointAt(current);
            const auto room=current==root?rootRoom:size_t(state(current).owner);
            const auto cost=state(current).cost;
            ++answer.expanded;consider(current,at,room,cost);
            if((answer.found || !partial) && !remainingCount) break;
            if(answer.found && (cost+movementCost.observation)*minimumFactor>=best) break;
            for(auto row=unseen.lower_bound(int(at.y)-teleportRange);row!=unseen.end() && row->first<=int(at.y)+teleportRange;++row) {
                auto entry=row->second.lower_bound({int(at.x)-teleportRange,0});
                while(entry!=row->second.end() && entry->first<=int(at.x)+teleportRange) {
                    if(cancelled()) {answer.found=false;answer.route.outcome=NavigationOutcome::Cancelled;return answer;}
                    const auto next=entry->second;const auto end=pointAt(next);
                    if(legalTeleport(at,room,end,state(next).owner)) {
                        auto &node=state(next);node.parent=current;node.cost=cost+castCost;
                        if(memoryExhausted || allocatedCells>1024*1024) return resourceLimited();
                        queue.push_back(next);entry=row->second.erase(entry);
                    } else ++entry;
                }
            }
        }
    } else {
        NavigationDerivedCache localCache;
        auto &cache=derivedCache?*derivedCache:localCache;
        const auto published=grid.freezeMovement?grid.freezeMovement():std::shared_ptr<const NavigationSnapshot>{};
        const int originX=published?published->originX:0,originY=published?published->originY:0;
        std::map<std::pair<int,int>,std::shared_ptr<const NavigationDerivedCache::Block>> blocks;
        if(!memory.reserve(8*1024*1024)) return resourceLimited();
        const auto blockAt=[&](int column,int row) -> const NavigationDerivedCache::Block * {
            const std::pair key{NavigationSnapshot::blockCoordinate(column+originX),NavigationSnapshot::blockCoordinate(row+originY)};
            auto found=blocks.find(key);
            if(found==blocks.end()) {
                if(blocks.size()>=1024) return nullptr;
                found=blocks.emplace(key,walkingBlock(grid,key.first,key.second,originX,originY,cache)).first;
            }
            return found->second.get();
        };
        const auto blockCell=[&](int column,int row) {
            const int globalX=column+originX,globalY=row+originY;
            return (globalY-NavigationSnapshot::blockCoordinate(globalY)*32)*32+
                globalX-NavigationSnapshot::blockCoordinate(globalX)*32;
        };
        if(targets.size()>1 && !partial) {
            using ComponentKey=std::tuple<int,int,uint16_t>;
            std::set<ComponentKey> reachable;
            std::set<ComponentKey> wanted;
            std::vector<ComponentKey> pending;
            bool complete=true;
            for(const auto &[row,entries]:remaining) for(const auto &[column,index]:entries) {
                (void)index;
                const auto *block=blockAt(column,row);
                if(!block) {complete=false;break;}
                wanted.emplace(NavigationSnapshot::blockCoordinate(column+originX),
                    NavigationSnapshot::blockCoordinate(row+originY),block->components[blockCell(column,row)]);
            }
            const auto admit=[&](int column,int row) {
                if(!grid.walkable(column,row,playerMovement)) return;
                const auto *block=blockAt(column,row);
                if(!block) {complete=false;return;}
                const ComponentKey component{NavigationSnapshot::blockCoordinate(column+originX),
                    NavigationSnapshot::blockCoordinate(row+originY),block->components[blockCell(column,row)]};
                wanted.erase(component);
                if(reachable.insert(component).second) pending.push_back(component);
            };
            admit(int(from.x),int(from.y));
            if(from.x!=std::floor(from.x)+.5f || from.y!=std::floor(from.y)+.5f)
                for(const auto &[dx,dy]:walkingDirections) {
                    const Vec next{float(int(from.x)+dx)+.5f,float(int(from.y)+dy)+.5f};
                    if(grid.segment(from,next,{},playerMovement) && grid.nativeMovementSegment(from,next,playerMovement))
                        admit(int(next.x),int(next.y));
                }
            for(size_t head=0;complete && !wanted.empty() && head<pending.size();++head) {
                if(cancelled()) {answer.found=false;answer.route.outcome=NavigationOutcome::Cancelled;return answer;}
                if(pending.size()>65536) {complete=false;break;}
                const auto [blockX,blockY,component]=pending[head];
                const auto block=blocks.at({blockX,blockY});
                for(int row=0;row<32;++row) for(int column=0;column<32;++column) {
                    if(row!=0 && row!=31 && column!=0 && column!=31) continue;
                    const int cell=row*32+column;
                    if(block->components[cell]!=component) continue;
                    for(size_t direction=0;direction<walkingDirections.size();++direction) {
                        if(!(block->edges[cell]&(1u<<direction))) continue;
                        const auto [dx,dy]=walkingDirections[direction];
                        if(column+dx>=0 && column+dx<32 && row+dy>=0 && row+dy<32) continue;
                        admit(blockX*32+column+dx-originX,blockY*32+row+dy-originY);
                    }
                }
            }
            if(complete) for(auto &[row,entries]:remaining) {
                for(auto entry=entries.begin();entry!=entries.end();) {
                    const auto *block=blockAt(entry->first,row);
                    if(!block) {++entry;continue;}
                    const ComponentKey component{NavigationSnapshot::blockCoordinate(entry->first+originX),
                        NavigationSnapshot::blockCoordinate(row+originY),block->components[blockCell(entry->first,row)]};
                    if(!reachable.contains(component)) {entry=entries.erase(entry);--remainingCount;}
                    else ++entry;
                }
            }
            if(!complete) blocks.clear();
        }
        using Entry=std::pair<float,size_t>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;queue.emplace(0,root);
        size_t queueCapacity=1;
        while(!queue.empty()) {
            if(cancelled()) {answer.found=false;answer.route.outcome=NavigationOutcome::Cancelled;return answer;}
            const auto [cost,current]=queue.top();queue.pop();if(cost!=state(current).cost) continue;
            const Vec at=current==root?from:pointAt(current);++answer.expanded;consider(current,at,0,cost);
            if((answer.found || !partial) && !remainingCount) break;
            if(answer.found && (cost+movementCost.observation)*minimumFactor>=best) break;
            const bool centered=at.x==std::floor(at.x)+.5f && at.y==std::floor(at.y)+.5f;
            uint8_t edges=0xff;
            if(centered) {
                const auto *block=blockAt(int(at.x),int(at.y));
                if(!block) return resourceLimited();
                edges=block->edges[blockCell(int(at.x),int(at.y))];
            }
            for(size_t direction=0;direction<walkingDirections.size();++direction) {
                if(!(edges&(1u<<direction))) continue;
                const auto [dx,dy]=walkingDirections[direction];
                const int column=int(at.x)+dx,row=int(at.y)+dy;
                if(!grid.walkable(column,row,playerMovement)) continue;
                const size_t next=size_t(row)*grid.width+column;const auto end=pointAt(next);
                const float nextCost=cost+(end-at).length()*movementCost.walkUnit;
                auto &node=state(next);
                if(memoryExhausted || allocatedCells>1024*1024) return resourceLimited();
                if(nextCost>=node.cost || (!centered && (!grid.segment(at,end,{},playerMovement) ||
                    !grid.nativeMovementSegment(at,end,playerMovement)))) continue;
                if(queue.size()+1>queueCapacity) {
                    const size_t nextCapacity=queueCapacity*2;
                    if(!memory.reserve((nextCapacity-queueCapacity)*sizeof(Entry)*2)) return resourceLimited();
                    queueCapacity=nextCapacity;
                }
                node.parent=current;node.cost=nextCost;queue.emplace(nextCost,next);
                if(allocatedCells>1024*1024 || queue.size()>1024*1024) return resourceLimited();
            }
        }
    }
    if(answer.found) answer.route.outcome=NavigationOutcome::Complete;
    else if(partial && partialCell!=none && partialCell!=root && targets.size()==1 &&
        partialDistance+1<(targets.front().point-from).length()) {
        selected=partialCell;answer.route.outcome=NavigationOutcome::Partial;
    } else return answer;
    for(size_t current=selected;current!=root;current=state(current).parent) answer.route.points.push_front(pointAt(current));
    if(answer.found && (answer.route.points.empty() || (answer.route.points.back()-targets[answer.candidate].point).length()>.01f))
        answer.route.points.push_back(targets[answer.candidate].point);
    if(teleportRange==0 && answer.route.points.size()>1) {
        std::deque<Vec> compact;Vec anchor=from;
        for(size_t first=0;first<answer.route.points.size();) {
            if(cancelled()) {answer.found=false;answer.route.points.clear();answer.route.outcome=NavigationOutcome::Cancelled;return answer;}
            size_t last=first;
            while(last+1<answer.route.points.size() && (answer.route.points[last+1]-anchor).length()<=48 &&
                grid.segment(anchor,answer.route.points[last+1],{},playerMovement) &&
                grid.nativeMovementSegment(anchor,answer.route.points[last+1],playerMovement)) ++last;
            anchor=answer.route.points[last];compact.push_back(anchor);first=last+1;
        }
        answer.route.points=std::move(compact);
    }
    return answer;
}
}