#include "exploration_memory.hpp"
#include "gameplay/units/coordinates.hpp"
#include "client/remote_town.hpp"
#include "world/navigation_planner.hpp"
#include "world/navigation_snapshot.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace d2x {
namespace {
using Cell=DebugExplorationMemory::Cell;
constexpr std::array<Cell,4> normals{{{1,0},{0,1},{-1,0},{0,-1}}};
float gap(Cell a,Cell b) {return Vec{float(a.first-b.first),float(a.second-b.second)}.length();}
uint64_t hash(uint64_t value,uint64_t next) {return (value^next)*1099511628211ull;}
struct PlanStageTimer {
    using Clock=std::chrono::steady_clock;
    double *elapsed{};
    Clock::time_point began=Clock::now();
    void next(double *stage) {
        const auto now=Clock::now();
        if(elapsed) *elapsed+=std::chrono::duration<double,std::milli>(now-began).count();
        elapsed=stage;began=now;
    }
    ~PlanStageTimer() {next(nullptr);}
};
}
DebugExplorationMemory::Cell DebugExplorationMemory::playerCell(Vec position) {
    const auto cell=nativeFeetCell(position);
    return {int(cell.x),int(cell.y)};
}
uint16_t DebugExplorationMemory::Area::mask(int gx,int gy) const {
    if(gx<x || gy<y || gx>=x+width || gy>=y+height) return 0xffff;
    const auto block=maskBlocks.find({gx/32,gy/32});
    return block==maskBlocks.end()?0xffff:(*block->second)[size_t(gy%32)*32+gx%32];
}
std::vector<uint16_t> DebugExplorationMemory::Area::flatMasks() const {
    std::vector<uint16_t> result(size_t(width)*height,0xffff);
    for(const auto &[at,block]:maskBlocks)
        for(int row=0;row<32;++row)
            std::copy_n(block->begin()+row*32,32,result.begin()+size_t(at.second*32+row-y)*width+at.first*32-x);
    return result;
}
bool DebugExplorationMemory::Area::clear(int gx,int gy) const {
    const auto value=mask(gx,gy);return value!=0xffff && !(value&playerMovement.mask);
}
bool DebugExplorationMemory::Area::stand(int gx,int gy) const {
    return clear(gx,gy) && clear(gx-1,gy) && clear(gx+1,gy) && clear(gx,gy-1) && clear(gx,gy+1);
}
DebugExplorationMemory::Passage *DebugExplorationMemory::Area::passage(uint64_t id) {
    const auto it=std::find_if(passages.begin(),passages.end(),[&](const auto &p){return p.id==id;});
    return it==passages.end()?nullptr:&*it;
}
DebugExplorationMemory::Area *DebugExplorationMemory::area(uint16_t id) {
    const auto it=areas_.find(id);return it==areas_.end()?nullptr:&it->second;
}
DebugExplorationMemory::Area *DebugExplorationMemory::observe(const RemoteTown &town,const OnlineView &view,const Grid &grid,Vec position) {
    using Clock=std::chrono::steady_clock;
    const auto started=Clock::now();
    if(game_!=view.gameGeneration) {areas_.clear();observations_.clear();metrics={};current_.reset();game_=view.gameGeneration;}
    const auto level=town.read().area;const auto origin=town.read().origin;
    if(!level || !origin || !town.map()) return nullptr;
    if(current_!=level) {
        if(current_ && areas_.contains(*current_)) areas_.at(*current_).previous.reset();
        current_=level;
        areas_[*level].previous.reset();
    }
    auto &m=areas_[*level];m.level=*level;
    auto &observation=observations_[*level];
    std::set<Cell> dirtyTiles;
    const auto mark=[&](int x,int y) {
        dirtyTiles.emplace(x/5,y/5);
    };
    const auto sketch=town.navigationSketch(*level);
    if(m.sketch!=sketch) {m.sketch=sketch;++m.revision;}
    const auto sample=town.read().mapLoading.terrainRevision,geometry=grid.obstacleRevision;
    bool passagesChanged=false;
    if(m.maskBlocks.empty() || sample!=m.sample || geometry!=m.geometry) {
        const auto sampleStarted=Clock::now();
        const auto source=grid.freezeMovement?grid.freezeMovement():nullptr;
        m.sample=sample;m.geometry=geometry;
        // Accept complete client collision independently of viewport/automap.
        // Keep both sides of walking seams; area changes must not erase feet.
        std::vector<std::pair<Cell,uint16_t>> observed;
        int left=m.width?m.x:65535,top=m.height?m.y:65535;
        int right=m.width?m.x+m.width:0,bottom=m.height?m.y+m.height:0;
        bool ownershipChanged=false;
        for(const auto &tile:town.knownNavigationTerrain()) {
            const int tx=tile.x,ty=tile.y,gx=tx*5,gy=ty*5;
            if(gx+5<=origin->x || gy+5<=origin->y || gx>=origin->x+grid.width || gy>=origin->y+grid.height) continue;
            const auto old=m.terrainLevels.find({tx,ty});
            const bool changedOwner=old==m.terrainLevels.end() || old->second!=tile.level;
            ownershipChanged|=changedOwner;
            m.terrainLevels[{tx,ty}]=tile.level;
            if(!changedOwner && source && observation.source &&
                source->sameGlobalRegion(*observation.source,gx,gy,gx+4,gy+4)) {++metrics.reusedTiles;continue;}
            for(int dy=0;dy<5;++dy) for(int dx=0;dx<5;++dx) {
                const int x=gx+dx,y=gy+dy;
                if(x<0 || y<0 || x>65535 || y>65535) continue;
                if(changedOwner) mark(x,y);
                ++metrics.sampledCells;
                const auto mask=grid.movementMask(x-origin->x,y-origin->y);
                if(mask==0xffff || m.mask(x,y)==mask) continue;
                mark(x,y);
                observed.push_back({{x,y},mask});
                left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);
            }
        }
        if(!observed.empty()) {
            // Grow by blocks to avoid reallocating for each newly observed strip.
            left=left/32*32;top=top/32*32;right=(right+31)/32*32;bottom=(bottom+31)/32*32;
            if(left!=m.x || top!=m.y || right!=m.x+m.width || bottom!=m.y+m.height) {
                const int width=right-left,height=bottom-top;
                if(uint64_t(width)*height>8*1024*1024) throw std::runtime_error("Observed terrain storage budget exhausted");
                m.x=left;m.y=top;m.width=width;m.height=height;
            }
            std::map<Cell,std::shared_ptr<Area::MaskBlock>> changed;
            for(const auto &[cell,mask]:observed) {
                const Cell key{cell.first/32,cell.second/32};auto &block=changed[key];
                if(!block) {
                    const auto old=m.maskBlocks.find(key);
                    block=std::make_shared<Area::MaskBlock>();
                    if(old==m.maskBlocks.end()) block->fill(0xffff);else *block=*old->second;
                    metrics.copiedMaskBytes+=sizeof(Area::MaskBlock);
                }
                (*block)[size_t(cell.second%32)*32+cell.first%32]=mask;
            }
            for(auto &[at,block]:changed) m.maskBlocks[at]=std::move(block);
            ++m.revision;
        }
        if(ownershipChanged && observed.empty()) ++m.revision;
        passagesChanged=!observed.empty() || ownershipChanged;
        observation.source=source;
        metrics.sampleMs+=std::chrono::duration<double,std::milli>(Clock::now()-sampleStarted).count();
    }
    // Collision pages can be acquired before the units on their ground.
    // Project only actual 07 subscriptions onto space; loaded neighbouring
    // pages and automap drawings never prove that their population was seen.
    bool unitsChanged=false;
    for(const auto &room:town.navigationRooms()) {
        if(!room.loaded || room.level!=m.level || !view.world.rooms.contains(
            {uint8_t(room.level),uint16_t(room.x/5),uint16_t(room.y/5)})) continue;
        // Unit observation is remembered for this game. An unchanged, already
        // subscribed extent cannot add new cells on every frontend frame.
        if(!observation.unitRooms.emplace(room.level,room.x,room.y,room.width,room.height).second) continue;
        for(int y=room.y/5;y<(room.y+room.height)/5;++y)
            for(int x=room.x/5;x<(room.x+room.width)/5;++x)
                if(m.unitObserved.emplace(x,y).second) {
                    unitsChanged=true;
                    mark(x*5,y*5);
                }
    }
    if(unitsChanged) ++m.revision;
    if(passagesChanged || unitsChanged) {
        const auto boundaryStarted=Clock::now();
        std::set<Cell> dirty;
        // Include the next cell's footprint and all local evidence neighbours.
        for(const auto &[tx,ty]:dirtyTiles)
            for(int y=std::max(m.y,ty*5-2);y<std::min(m.y+m.height,ty*5+7);++y)
                for(int x=std::max(m.x,tx*5-2);x<std::min(m.x+m.width,tx*5+7);++x) dirty.emplace(x,y);
        rebuildPassages(m,observation,dirty);metrics.boundaryCells+=dirty.size();
        metrics.boundaryMs+=std::chrono::duration<double,std::milli>(Clock::now()-boundaryStarted).count();
    }
    // Only confirmed host positions form the return graph. Do not infer an edge
    // across a correction, teleport, loading transition, or an unobserved bend.
    const auto actor=view.world.units.find({0,view.load.playerUnitId.value_or(0)});
    if(view.world.playerPosition && actor!=view.world.units.end()) {
        const auto at=*view.world.playerPosition;const Cell cell{at.x,at.y};
        const auto discontinuity=actor->second.positionDiscontinuity;
        if(m.stand(cell.first,cell.second)) {
            m.walked.try_emplace(cell);
            if(m.previous && m.discontinuity==discontinuity && *m.previous!=cell && gap(*m.previous,cell)<=8) {
                const auto previous=*m.previous;
                const int count=std::max(std::abs(cell.first-previous.first),std::abs(cell.second-previous.second));
                std::vector<Cell> segment{previous};bool valid=true;
                for(int i=1;i<=count;++i) {
                    const Cell next{int(std::lround(previous.first+float(cell.first-previous.first)*i/count)),
                        int(std::lround(previous.second+float(cell.second-previous.second)*i/count))};
                    if(!m.stand(next.first,next.second)) {valid=false;break;}
                    segment.push_back(next);
                }
                if(valid) for(size_t i=1;i<segment.size();++i) {
                    m.walked[segment[i-1]].insert(segment[i]);m.walked[segment[i]].insert(segment[i-1]);
                }
            }
            m.previous=cell;m.discontinuity=discontinuity;
        } else m.previous.reset();
    }
    (void)position;
    const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
    ++metrics.observations;metrics.observationMs+=elapsed;metrics.maxObservationMs=std::max(metrics.maxObservationMs,elapsed);
    return &m;
}
void DebugExplorationMemory::rebuildPassages(Area &m,Observation &observation,const std::set<Cell> &dirty) {
    std::vector<Passage> next;
    size_t edgeIndex=0;
    for(const bool units:{false,true}) for(const auto &normal:normals) {
        auto &retained=observation.edges[edgeIndex++];
        for(const auto &[x,y]:dirty) {
            retained.erase({x,y});
            // Both kinds are open directions with a legal player footprint.
            // Collision knowledge and unit observation advance independently.
            const auto ownership=m.terrainLevels.find({x/5,y/5});
            if(ownership==m.terrainLevels.end() || ownership->second!=m.level) continue;
            const int nx=x+normal.first,ny=y+normal.second;
            if(units) {
                const auto next=m.terrainLevels.find({nx/5,ny/5});
                if(!m.unitObserved.contains({x/5,y/5}) || m.unitObserved.contains({nx/5,ny/5}) ||
                    next==m.terrainLevels.end() || next->second!=m.level ||
                    !m.stand(x,y) || !m.stand(nx,ny)) continue;
            } else if(!m.clear(x,y) || m.mask(nx,ny)!=0xffff ||
                !m.stand(x-normal.first,y-normal.second)) continue;
            retained.emplace(x,y);
        }
        // Components and identity matching retain exactly the full scan order.
        auto edges=retained;
        while(!edges.empty()) {
            Passage p;p.direction=normal;p.units=units;
            std::vector<Cell> pending{*edges.begin()};edges.erase(edges.begin());
            for(size_t i=0;i<pending.size();++i) {
                const auto cell=pending[i];p.boundary.insert(cell);
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    const Cell adjacent{cell.first+dx,cell.second+dy};
                    if(edges.erase(adjacent)) pending.push_back(adjacent);
                }
            }
            for(const auto &cell:p.boundary) {
                // The footprint already establishes the required opening
                // width. Requiring three approach centres loses narrow doors.
                // On known ground cross the observation boundary, rather than
                // stopping on its already observed side.
                const int step=units?1:-1;
                p.approaches.emplace_back(cell.first+step*normal.first,cell.second+step*normal.second);
            }
            if(p.approaches.empty()) continue;
            p.evidence=hash(1469598103934665603ull,units);
            for(const auto &cell:p.boundary) {
                p.evidence=hash(hash(p.evidence,uint32_t(cell.first)),uint32_t(cell.second));
                // Local observations, including door changes, can justify a retry.
                for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
                    p.evidence=hash(p.evidence,m.mask(cell.first+dx,cell.second+dy));
                    if(units) p.evidence=hash(p.evidence,m.unitObserved.contains({(cell.first+dx)/5,(cell.second+dy)/5}));
                }
            }
            const Passage *prior=nullptr;size_t overlap=0;
            for(const auto &old:m.passages) if(old.units==units && old.direction==p.direction) {
                size_t shared=0;for(const auto &cell:p.boundary) shared+=old.boundary.contains(cell);
                if(shared>overlap) {overlap=shared;prior=&old;}
            }
            if(!prior) {
                // Keep a branch identity as its observed opening advances.
                // Tangentially separate branches must never inherit each other.
                float closest=13;
                for(const auto &old:m.passages) if(old.units==units && old.direction==p.direction && old.deferred!=old.evidence) {
                    const auto a=old.approaches[old.approaches.size()/2],b=p.approaches[p.approaches.size()/2];
                    const int dx=b.first-a.first,dy=b.second-a.second;
                    const int forward=dx*normal.first+dy*normal.second,lateral=std::abs(dx*normal.second-dy*normal.first);
                    const float span=gap(a,b);
                    if(forward>0 && lateral<=3 && span<closest) {closest=span;prior=&old;}
                }
            }
            // A split gets distinct identities; its unchanged failed evidence
            // is inherited, so another nearby standing point isn't a new try.
            if(prior) {
                const bool used=std::any_of(next.begin(),next.end(),[&](const auto &q){return q.id==prior->id;});
                p.id=used?m.nextPassage++:prior->id;
                if(prior->deferred==prior->evidence && p.evidence==prior->evidence) p.deferred=p.evidence;
            } else p.id=m.nextPassage++;
            next.push_back(std::move(p));
        }
    }
    m.passages=std::move(next);
    if(m.commitment && !m.passage(*m.commitment)) m.commitment.reset();
}
void DebugExplorationMemory::defer(Area &m,uint64_t id) {
    if(auto *p=m.passage(id)) {p->deferred=p->evidence;++m.revision;}
    if(m.commitment==id) m.commitment.reset();
}
bool DebugExplorationMemory::Execution::requiresDoor(Cell to) const {
    // Door operations are the difference between two live collision views,
    // not between remembered ground and an unpublished landing footprint.
    // Planning and route validation must use this same predicate.
    if(!collision || !collisionGrid || !landingCollision || !landingGrid) return false;
    if(!collisionGrid->walkable(Vec{float(to.first-collision->originX)+.5f,
        float(to.second-collision->originY)+.5f},playerMovement)) return false;
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
        if(nativeCollisionSizeCell(playerMovement.size,dx,dy) &&
            landingCollision->mask(to.first+dx-landingCollision->originX,
                to.second+dy-landingCollision->originY)==0xffff) return false;
    return !landingAllowed(to);
}
bool DebugExplorationMemory::Execution::landingAllowed(Cell to) const {
    return landingCollision && landingGrid && landingGrid->walkable(
        Vec{float(to.first-landingCollision->originX)+.5f,float(to.second-landingCollision->originY)+.5f},playerMovement);
}
bool DebugExplorationMemory::Execution::castRayAllowed(Cell from,Cell to,bool restricted) const {
    if(!restricted) return true;
    if(!landingCollision || !landingGrid) return false;
    const Vec offset{float(landingCollision->originX),float(landingCollision->originY)};
    return landingGrid->interactionSegment(Vec{float(from.first),float(from.second)}-offset,
        Vec{float(to.first),float(to.second)}-offset,2,{},0x0804,2);
}
bool DebugExplorationMemory::Execution::castAllowed(Cell from,Cell to,bool restricted) const {
    if(!landingAllowed(to) || !castRayAllowed(from,to,restricted)) return false;
    const auto roomAt=[&](Cell at) {
        for(size_t i=0;i<rooms.size();++i) {
            const auto &r=rooms[i];
            if(at.first>=r.x && at.second>=r.y && at.first<r.x+r.width && at.second<r.y+r.height) return i;
        }
        return rooms.size();
    };
    const auto source=roomAt(from),target=roomAt(to);
    return source<rooms.size() && target<rooms.size() && (source==target ||
        std::find(rooms[source].near.begin(),rooms[source].near.end(),target)!=rooms[source].near.end());
}
bool DebugExplorationMemory::routeValid(const Area &m,const Plan &plan,int range,bool restricted,const Execution *execution,RouteRejection *rejection) {
    const auto reject=[&](const char *reason,size_t point=0) {
        if(rejection) {
            *rejection={reason,point};
            if(point<plan.points.size()) {
                const auto from=plan.points[point?point-1:0],to=plan.points[point];
                rejection->from={from.x,from.y};rejection->to={to.x,to.y};
            }
        }
        return false;
    };
    if(plan.points.empty()) return reject("empty-plan");
    Grid grid;grid.width=m.width;grid.height=m.height;
    grid.collisionSample=[&](int x,int y,EntityId)->uint16_t {
        const int gx=x+m.x,gy=y+m.y;const auto known=m.mask(gx,gy);
        if(known==0xffff || !execution || !execution->collision) return known;
        const auto &live=*execution->collision;const auto mask=execution->collisionGrid->movementMask(gx-live.originX,gy-live.originY);
        return mask==0xffff?known:mask;
    };
    for(size_t i=0;i<plan.points.size();++i) {
        const auto p=plan.points[i];const Vec to{float(p.x-m.x)+.5f,float(p.y-m.y)+.5f};
        if(!grid.walkable(to,playerMovement) && (i || range<=0)) return reject("walking-footprint",i);
        if(!i) continue;
        if(range>0 && execution && !plan.doorFallback && execution->requiresDoor({p.x,p.y}))
            return reject("door-bypass-recheck",i);
        const auto q=plan.points[i-1];const Vec from{float(q.x-m.x)+.5f,float(q.y-m.y)+.5f};
        if(grid.nativeMovementSegment(from,to,playerMovement)) continue;
        if(range<=0) return reject("walking-edge",i);
        if((to-from).length()>range) return reject("cast-range",i);
        if(execution && !execution->castAllowed({q.x,q.y},{p.x,p.y},restricted)) {
            if(!rejection) return false;
            if(!execution->landingAllowed({p.x,p.y})) return reject("cast-landing",i);
            if(!execution->castRayAllowed({q.x,q.y},{p.x,p.y},restricted)) return reject("cast-ray",i);
            return reject("cast-room-neighborhood",i);
        }
        if(!execution && restricted && !grid.interactionSegment(from-Vec{.5f,.5f},to-Vec{.5f,.5f},2,{},0x0804,2))
            return reject("known-cast-ray",i);
    }
    return true;
}
DebugExplorationMemory::Plan DebugExplorationMemory::plan(const Area &m,Vec from,int range,bool restricted,
        std::optional<Vec> goal,float vicinity,uint64_t committed,bool exploreGoal,
        const std::optional<std::vector<Cell>> &approaches,const Execution *execution,bool preferDoorBypass,
        std::optional<Cell> preferredDirection,PlanProfile *profile) {
    PlanStageTimer timing{profile?&profile->prepareMs:nullptr};
    if(profile) {++profile->calls;profile->storageCells+=uint64_t(m.width)*m.height;}
    Plan result;if(m.maskBlocks.empty()) {result.outcome=Plan::Outcome::StartUnavailable;return result;}
    if(!goal && !committed && m.commitment) committed=*m.commitment;
    const Cell source=playerCell(from);
    const auto index=[&](Cell cell){return (cell.second-m.y)*m.width+cell.first-m.x;};
    const auto cell=[&](int i)->Cell{return {i%m.width+m.x,i/m.width+m.y};};
    // Flatten the live overlay once, not on every footprint/edge lookup.
    // Knowledge remains immutable; unpublished cells are still unknown.
    auto masks=m.flatMasks();
    if(execution && execution->collision) {
        const auto &live=*execution->collision;
        for(int y=std::max(m.y,live.originY);y<std::min(m.y+m.height,live.originY+live.height);++y) {
            navigationWorkCheckpoint();
            for(int x=std::max(m.x,live.originX);x<std::min(m.x+m.width,live.originX+live.width);++x) {
                const auto i=size_t(index({x,y}));if(masks[i]==0xffff) continue;
                const auto mask=execution->collisionGrid->movementMask(x-live.originX,y-live.originY);
                if(mask!=0xffff) masks[i]=mask;
            }
        }
    }
    Grid known;known.width=m.width;known.height=m.height;
    known.collisionSample=[&](int x,int y,EntityId)->uint16_t {
        return x<0 || y<0 || x>=m.width || y>=m.height?0xffff:masks[size_t(y)*m.width+x];
    };
    const auto local=[&](Cell at){return Vec{float(at.first-m.x)+.5f,float(at.second-m.y)+.5f};};
    std::vector<int8_t> standing(masks.size(),-1);
    const auto stand=[&](int x,int y) {
        if(x<m.x || y<m.y || x>=m.x+m.width || y>=m.y+m.height) return false;
        auto &cached=standing[size_t(index({x,y}))];
        if(cached<0) cached=int8_t(known.walkable(local({x,y}),playerMovement));
        return cached!=0;
    };
    std::map<Cell,size_t> pages;
    if(range>0 && execution) for(size_t i=0;i<execution->rooms.size();++i) {
        const auto &room=execution->rooms[i];
        for(int y=room.y/5;y<(room.y+room.height)/5;++y)
            for(int x=room.x/5;x<(room.x+room.width)/5;++x) pages[{x,y}]=i;
    }
    const auto castAllowed=[&](Cell from,Cell to) {
        if(!execution) return true;
        const auto sourcePage=pages.find({from.first/5,from.second/5});
        const auto targetPage=pages.find({to.first/5,to.second/5});
        if(sourcePage==pages.end() || targetPage==pages.end()) return false;
        const auto &near=execution->rooms[sourcePage->second].near;
        return sourcePage->second==targetPage->second ||
            std::find(near.begin(),near.end(),targetPage->second)!=near.end();
    };
    Cell start=source;float nearest=std::numeric_limits<float>::max();
    for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
        const Cell at{source.first+dx,source.second+dy};const float d=gap(source,at);
        if(d<nearest && d<2 && stand(at.first,at.second) && known.nativeMovementSegment(local(source),local(at),playerMovement)) {nearest=d;start=at;}
    }
    // Casting can leave a blocked source; only its landing must be standable.
    if(range>0 && m.mask(source.first,source.second)!=0xffff) {start=source;nearest=0;}
    const auto unavailable=[&](Plan::Outcome reason) {
        result.outcome=reason;
        if(execution) {
            timing.next(nullptr);if(profile) ++profile->structuralCalls;
            const auto structural=plan(m,from,range,restricted,goal,vicinity,committed,exploreGoal,approaches,nullptr,true,preferredDirection,profile);
            timing.next(profile?&profile->finishMs:nullptr);
            if(!structural.points.empty()) result.outcome=Plan::Outcome::ExecutionBlocked;
        }
        return result;
    };
    if(nearest==std::numeric_limits<float>::max()) return unavailable(Plan::Outcome::StartUnavailable);
    std::map<int,const Passage *> destinations;
    for(const auto &p:m.passages) if((!goal || exploreGoal) && (!committed || p.id==committed) && p.deferred!=p.evidence)
        for(const auto &at:p.approaches) if(stand(at.first,at.second)) {
            auto [entry,fresh]=destinations.emplace(index(at),&p);
            if(!fresh && p.units && !entry->second->units) entry->second=&p;
        }
    if(!goal && destinations.empty()) return unavailable(Plan::Outcome::Unreachable);
    // A collision publication edge does not prove there is a passage beyond
    // it. First advance into reachable, known ground whose units are unseen;
    // only then probe data edges. Keep an explicit bearing or active branch's
    // existing contract. Both choices use the same graph and movement rules.
    const auto evidenceRank=[&](const Passage &p) {
        if(goal || committed) return 0;
        if(p.units) return 0;
        if(m.expectedBounds) {
            const auto &b=*m.expectedBounds;
            const bool outside=std::all_of(p.boundary.begin(),p.boundary.end(),[&](const auto &at) {
                const int x=at.first+p.direction.first,y=at.second+p.direction.second;
                return x<b[0] || y<b[1] || x>=b[0]+b[2] || y>=b[1]+b[3];
            });
            if(outside) return 2;
        }
        // An explicit experience bearing may favor forward probing over an
        // opposite observation direction; acquired collision still owns every
        // edge. Unbiased exploration prefers the stronger observation evidence.
        return preferredDirection?0:1;
    };
    std::map<uint64_t,int> evidenceRanks;
    int preferredRank=2;
    for(const auto &[at,p]:destinations) {
        (void)at;
        const auto [entry,fresh]=evidenceRanks.try_emplace(p->id,0);
        if(fresh) entry->second=evidenceRank(*p);
        preferredRank=std::min(preferredRank,entry->second);
    }
    std::set<int> goalCells;
    if(approaches) for(const auto &at:*approaches)
        if(stand(at.first,at.second)) goalCells.insert(index(at));
    if(goal && !approaches) {
        const int radius=int(std::ceil(vicinity));
        for(int y=int(goal->y)-radius;y<=int(goal->y)+radius;++y)
            for(int x=int(goal->x)-radius;x<=int(goal->x)+radius;++x)
                if((Vec{float(x),float(y)}-*goal).length()<=vicinity && stand(x,y)) goalCells.insert(index({x,y}));
    }
    if(goalCells.empty() && destinations.empty()) return unavailable(Plan::Outcome::Unreachable);
    int goalLeft=m.x+m.width,goalTop=m.y+m.height,goalRight=m.x,goalBottom=m.y;
    for(const auto i:goalCells) {
        const auto at=cell(i);goalLeft=std::min(goalLeft,at.first);goalRight=std::max(goalRight,at.first);
        goalTop=std::min(goalTop,at.second);goalBottom=std::max(goalBottom,at.second);
    }
    const auto heuristic=[&](Cell at) {
        if(!goal || goalCells.empty()) return 0.f;
        const float dx=float(std::max({goalLeft-at.first,0,at.first-goalRight}));
        const float dy=float(std::max({goalTop-at.second,0,at.second-goalBottom}));
        // Lower bound for both walked edges and 8 + 0.1 * distance casts.
        return Vec{dx,dy}.length()*(range>0?std::min(1.f,.1f+8.f/range):1.f);
    };
    // Map drawings (including town montages and saved map cells) are weak
    // structural bearings. They never supply standing cells or collision edges.
    std::set<Cell> structures;
    if(!goal && m.sketch) for(const auto pixel:m.sketch->ink) {
        const auto world=unproject(pixel);const int x=int(std::floor(world.x)),y=int(std::floor(world.y));
        if(x>=0 && y>=0 && x<=65535 && y<=65535 && m.mask(x,y)==0xffff) structures.emplace(x/8,y/8);
    }
    std::set<uint64_t> sketchDirections;
    for(const auto &p:m.passages) {
        const auto at=p.approaches[p.approaches.size()/2],normal=p.direction;
        for(int forward=8;forward<=80;forward+=8) {
            for(int side:{-8,0,8}) {
                const Cell hint{at.first+normal.first*forward-normal.second*side,
                    at.second+normal.second*forward+normal.first*side};
                if(structures.contains({hint.first/8,hint.second/8})) sketchDirections.insert(p.id);
            }
        }
    }
    const auto passagePenalty=[&](Cell at,const Passage &p) {
        float score=-(sketchDirections.contains(p.id)?6.f:0.f);
        if(goal) {
            const Vec toward=*goal-Vec{float(at.first),float(at.second)};
            const float remaining=toward.length();const auto normal=p.direction;
            // The hint supplies a bearing, never a traversable edge. Prefer
            // forward openings; real route cost still permits a needed detour.
            score+=1.25f*remaining;
            if(remaining>0) score+=24.f*(1-(toward.x*normal.first+toward.y*normal.second)/remaining);
        } else if(preferredDirection) {
            const auto normal=p.direction;
            const Vec axis=Vec{float(preferredDirection->first),float(preferredDirection->second)}.unit();
            // Experience supplies a bounded directional preference, not a
            // synthetic far-away goal that forces an exhaustive search.
            score+=24.f*(1-axis.x*normal.first-axis.y*normal.second);
        } else if(m.heading) {
            const auto heading=*m.heading,direction=p.direction;
            score+=12.f*(1-(heading.first*direction.first+heading.second*direction.second));
        }
        return score;
    };
    float minimumPenalty=std::numeric_limits<float>::max();
    for(const auto &[at,p]:destinations) if(evidenceRanks.at(p->id)==preferredRank)
        minimumPenalty=std::min(minimumPenalty,passagePenalty(cell(at),*p));
    const size_t count=masks.size();
    std::vector<float> costs(count,std::numeric_limits<float>::max());
    std::vector<int> parent(count,-1);
    // Sparse landing graph, including narrow passages and the real start.
    // Sampling controls graph density only; all objectives come from openings.
    std::map<Cell,std::vector<int>> landingBuckets;
    std::set<int> landings;
    timing.next(profile?&profile->landingsMs:nullptr);
    if(range>0) {
        const int stride=std::max(2,range/3);
        for(int y=m.y;y<m.y+m.height;++y) {
            navigationWorkCheckpoint();
            for(int x=m.x;x<m.x+m.width;++x)
            if(stand(x,y) && ((x%stride==0 && y%stride==0) ||
                (x%stride==0 && (!stand(x,y-1) || !stand(x,y+1))) ||
                (y%stride==0 && (!stand(x-1,y) || !stand(x+1,y))))) landings.insert(index({x,y}));
        }
        for(const auto &p:m.passages) for(const auto &at:p.approaches)
            if(stand(at.first,at.second)) landings.insert(index(at));
        for(const auto &[at,edges]:m.walked) { (void)edges;if(stand(at.first,at.second)) landings.insert(index(at)); }
        landings.insert(goalCells.begin(),goalCells.end());
        if(goal) {
            const Cell at{int(goal->x),int(goal->y)};
            if(stand(at.first,at.second)) landings.insert(index(at));
        }
        // A walk-through door is an executable operation, never an already
        // clear casting destination. Unpublished live landings also wait for
        // a later local leg; terrain memory alone cannot authorize a cast.
        if(execution) std::erase_if(landings,[&](int i){return !execution->landingAllowed(cell(i));});
        landings.insert(index(start)); // A cast may escape a blocked source.
        for(const int i:landings) {const auto at=cell(i);landingBuckets[{at.first/range,at.second/range}].push_back(i);}
    }
    if(profile) profile->landingCandidates+=landings.size();
    timing.next(profile?&profile->searchMs:nullptr);
    using Entry=std::tuple<float,float,int>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> pending;
    const int root=index(start);costs[size_t(root)]=0;pending.emplace(heuristic(start),0,root);
    const bool sourceLanding=!execution || execution->landingAllowed(start);
    int chosen=-1,bestRank=3;float best=std::numeric_limits<float>::max();const Passage *selected=nullptr;
    while(!pending.empty()) {
        navigationWorkCheckpoint();
        const auto [priority,cost,i]=pending.top();pending.pop();(void)priority;
        if(cost!=costs[size_t(i)]) continue;
        // A cheap speculative edge cannot terminate the search before a
        // reachable observation direction has been considered. If none is
        // reachable, retain the best speculative result from this traversal.
        // Include the minimum terminal penalty in the Dijkstra lower bound;
        // a distant hint's absolute distance must not force a full-map scan.
        if((!goal || goalCells.empty()) && bestRank==preferredRank && cost+minimumPenalty>best) break;
        ++result.expanded;if(profile) ++profile->expanded;const auto at=cell(i);
        if(goal && goalCells.contains(i)) {
            chosen=i;selected=nullptr;break;
        }
        if(const auto target=destinations.find(i);target!=destinations.end()) {
            const float score=cost+passagePenalty(at,*target->second);
            const int rank=evidenceRanks.at(target->second->id);
            if(rank<bestRank || (rank==bestRank && score<best)) {
                chosen=i;best=score;selected=target->second;bestRank=rank;
            }
        }
        const auto relax=[&](int next,float edge) {
            if(cost+edge>=costs[size_t(next)]) return;
            costs[size_t(next)]=cost+edge;parent[size_t(next)]=i;
            pending.emplace(cost+edge+heuristic(cell(next)),cost+edge,next);
        };
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if(!dx && !dy) continue;
            const Cell next{at.first+dx,at.second+dy};
            if(!stand(next.first,next.second) || !known.nativeMovementSegment(local(at),local(next),playerMovement)) continue;
            const auto trail=m.walked.find(at);
            const bool walked=trail!=m.walked.end() && trail->second.contains(next);
            // Recorded trails are cheaper; lack of a sampled trail edge must
            // not disconnect two cells joined by known legal collision.
            relax(index(next),(dx && dy?1.414214f:1.f)*(walked?1.f:1.25f));
        }
        if(range>0 && landings.contains(i)) for(int by=-1;by<=1;++by) for(int bx=-1;bx<=1;++bx) {
            const auto bucket=landingBuckets.find({at.first/range+bx,at.second/range+by});
            if(bucket==landingBuckets.end()) continue;
            for(const int next:bucket->second) {
                if(profile) ++profile->castCandidates;
                const auto to=cell(next);const float span=gap(at,to);if(span<2 || span>range || !castAllowed(at,to)) continue;
                if(next==root && !sourceLanding) continue;
                const bool rayClear=execution?execution->castRayAllowed(at,to,restricted):
                    !restricted || known.interactionSegment(local(at)-Vec{.5f,.5f},local(to)-Vec{.5f,.5f},2,{},0x0804,2);
                if(!rayClear) continue;
                relax(next,8.f+span*.1f);
            }
        }
    }
    timing.next(profile?&profile->finishMs:nullptr);
    if(chosen<0) return unavailable(Plan::Outcome::Unreachable);
    // A reachable observation passage is an executable result. A route in
    // terrain memory may depend on unloaded cast landings/room adjacency;
    // it cannot veto the passage that lets execution observe them again.
    // Structural classification belongs only to the no-route case above.
    result.outcome=selected?Plan::Outcome::Passage:Plan::Outcome::Goal;
    result.cost=costs[size_t(chosen)];
    if(selected) {result.passage=selected->id;result.evidence=selected->evidence;}
    for(int i=chosen;i>=0;i=parent[size_t(i)]) {const auto at=cell(i);result.points.push_back({uint16_t(at.first),uint16_t(at.second)});}
    std::reverse(result.points.begin(),result.points.end());
    if(range>0 && execution && execution->collision && execution->landingCollision) {
        const auto needsDoor=[&](OnlinePoint at) {
            return execution->requiresDoor({at.x,at.y});
        };
        result.doorFallback=std::any_of(result.points.begin(),result.points.end(),needsDoor);
        // Prefer a legal route around/over closed doors before committing an
        // opening operation, even when walking through the door is cheaper.
        // An endpoint inside the door footprint necessarily needs it opened.
        if(preferDoorBypass && result.doorFallback && !needsDoor(result.points.back())) {
            auto bypass=*execution;
            bypass.collision=bypass.landingCollision;bypass.collisionGrid=bypass.landingGrid;
            timing.next(nullptr);if(profile) ++profile->doorBypassCalls;
            auto alternative=plan(m,from,range,restricted,goal,vicinity,result.passage?result.passage:committed,
                exploreGoal,approaches,&bypass,false,preferredDirection,profile);
            timing.next(profile?&profile->finishMs:nullptr);
            if(alternative.outcome==result.outcome && alternative.passage==result.passage && !alternative.points.empty()) {
                alternative.expanded+=result.expanded;return alternative;
            }
            result.expanded+=alternative.expanded;
        }
    }
    if(result.points.size()>2) {
        const auto p=result.points[std::min(size_t(8),result.points.size()-1)];
        result.backtracking=m.walked.contains({p.x,p.y});
    }
    return result;
}
}
